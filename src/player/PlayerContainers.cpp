/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

 * CurseStone | Open Source server Software
 *
 * Copyright (C) 2026 CurseStoneTeam
 *
 * Developed by CurseStoneTeam.
 * Built for an open and extensible Minecraft server ecosystem.
 *
 * Licensed under the GNU Lesser General Public License v3.0 or later.
 * You may study, modify, and redistribute this software under its license.
 *
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 * Repository: https://github.com/CurseStoneTeam/CurseStone
 *
 */
#include "Player.h"
#include "world/World.h"
#include "world/Position.h"
#include "network/protocol/BlockEntityDataPacket.h"
#include "network/protocol/BlockEventPacket.h"
#include "network/protocol/ContainerOpenPacket.h"
#include "network/protocol/ContainerClosePacket.h"
#include "network/protocol/ContainerSetContentPacket.h"
#include "network/protocol/ContainerSetSlotPacket.h"
#include "utils/CRaii.h"
#include "tile/Tile.h"
#include "tile/TileChest.h"
#include "tile/TileFurnace.h"
#include "tile/TileSign.h"
#include "inventory/FurnaceInventory.h"
#include "inventory/Inventory.h"
#include "event/EventManager.h"
#include "event/inventory/InventoryCloseEvent.h"
#include "event/inventory/InventoryOpenEvent.h"
#include "event/block/SignChangeEvent.h"
#include "nbt/NBT.h"
#include "nbt/tag/CompoundTag.h"
#include "utils/MainLogger.hpp"
#include <array>
#include <cstdlib>
#include <string>

namespace cursestone {

// ============================================================
// broadcastChestBlockEvent — the chest lid animation + open/close
// sounds. The 0.15.10 client animates the chest block (and plays the
// wooden-open/wooden-close sound) ONLY off a BlockEventPacket aimed at
// the chest's block coordinates: case1 1 opens the lid, case1 0 closes
// it. Without it the chest opens its UI but stays visually shut and
// silent. Sent to everyone who tracks the chest's chunk (including the
// acting player), mirroring how the reference server spreads block
// events to chunk viewers.
// ============================================================
static void broadcastChestBlockEvent(cursestone::Player* actor,
                                     const cursestone::tile::TileChest& chest,
                                     int case1) {
    if (!actor || !actor->server())
        return;
    const auto& pos = chest.position();
    int32_t x = (int32_t)pos.x;
    int32_t y = (int32_t)pos.y;
    int32_t z = (int32_t)pos.z;

    auto pk = std::make_shared<network::protocol::BlockEventPacket>();
    pk->x = x;
    pk->y = y;
    pk->z = z;
    pk->case1 = case1;
    pk->case2 = 0;

    int32_t cx = x >> 4;
    int32_t cz = z >> 4;
    for (auto& viewer : actor->server()->playersSnapshot()) {
        if (!viewer)
            continue;
        if (viewer->getWorld() != actor->getWorld())
            continue;
        if (viewer.get() != actor && !viewer->canSeeBlock(cx, cz))
            continue;
        viewer->sendPacket(pk);
    }
}

// ============================================================
// openChestAt / closeOpenChest — the actual chest flow.
//
// TileChest is the single source of truth for to chest's contents
// (see TileChest.h). World::getTile/setTile is what makes the SAME
// TileChest instance come back every time this position is opened,
// instead of to fresh empty one — so items survive to close+reopen for
// as long as the process keeps running (not disk persistence yet, see
// the comment on World::getTile in World.h).
// ============================================================
void Player::openChestAt(int x, int y, int z) {
    auto* lvl = getWorld();
    if (!lvl)
        return;

    // Only one container open at a time per player (see openContainerWindow).
    // The furnace counts too: both windows share the single container window
    // id, and leaving open_furnace_ set would keep a half-open window whose
    // clicks the server can no longer route.
    if (open_chest_ || open_furnace_) {
        closeOpenChest();
    }

    // Reuse the existing TileChest at this position, or create one the
    // first time this chest is opened. World::getTile returns to
    // shared_ptr<tile::tile> (the base type — the registry can hold any
    // tile kind), so it needs to dynamic_cast back down to TileChest.
    // to null result after to non-null getTile() would mean to different
    // kind of tile-entity is registered at this position, which
    // shouldn't happen for to block we've just confirmed is block::CHEST,
    // but we guard it anyway rather than dereferencing to null pointer.
    std::shared_ptr<tile::TileChest> chest;
    auto existing = lvl->getTile(x, y, z);
    if (existing) {
        chest = std::dynamic_pointer_cast<tile::TileChest>(existing);
        if (!chest)
            return;
    } else {
        chest = std::make_shared<tile::TileChest>(
            cursestone::world::Position((double)x, (double)y, (double)z));
        lvl->setTile(x, y, z, chest);
    }

    // InventoryOpenEvent — to listener can cancel (e.g. zone protection,
    // to chest mid-trade) to block opening even though PlayerInteractEvent
    // already passed. Nothing is sent to the client and not viewer is
    // registered if this is cancelled.
    auto ev = event::EventManager::get().fire<event::inventory::InventoryOpenEvent>(
        std::static_pointer_cast<inventory::Inventory>(chest->inventory()), this);
    if (ev.isCancelled())
        return;

    chest->addViewer();
    open_chest_ = chest;
    openContainerWindow(); // sets container_window_id_ for every packet below

    // The client binds the container UI to the block actor at these
    // coords, so the BlockEntityData must exist client-side BEFORE the
    // ContainerOpen record arrives — same order a chunk stream produces
    // (chunk data, then tile NBTs). Inside the open batch the wrong order
    // leaves the 0.15.10 client binding a UI to a block entity it has
    // not seen yet.
    sendBlockEntityData(x, y, z, *chest, nullptr);

    auto op = std::make_shared<network::protocol::ContainerOpenPacket>();
    op->windowid = container_window_id_;
    op->type = network::protocol::ContainerOpenPacket::TYPE_CONTAINER;
    op->slots = (int16_t)chest->inventory()->getSize();
    op->x = x;
    op->y = y;
    op->z = z;
    sendPacket(op);

    // Lid up + open sound. Only when this opener is the FIRST viewer:
    // the lid must open once per open session, and it must stay up
    // while any other player still holds the chest open.
    if (chest->viewerCount() == 1)
        broadcastChestBlockEvent(this, *chest, 1);

    // Full slot dump so the client actually shows the contents. Can't
    // reuse BaseInventory::sendContents() — it hardcodes windowid=0
    // (correct for the player's own inventory, wrong here: it would
    // collide with SPECIAL_INVENTORY and get treated as to hotbar/main
    // inventory update instead of the chest window).
    auto content = std::make_shared<network::protocol::ContainerSetContentPacket>();
    content->windowid = container_window_id_;
    content->slots.reserve(chest->inventory()->getSize());
    for (int i = 0; i < chest->inventory()->getSize(); ++i) {
        content->slots.push_back(chest->inventory()->getItem(i));
    }
    sendPacket(content);

    if (srv_ && srv_->advancedLog()) {
        utils::MainLogger::get().info("[CHEST] " + cfg_.username + " opened chest at " +
                                      std::to_string(x) + "," + std::to_string(y) + "," +
                                      std::to_string(z));
    }
}

// broadcastChestSlotToViewers / broadcastFurnaceSlotToViewers — the
// vanilla co-viewer sync for open containers. Every player who has the
// SAME tile instance open (World::getTile hands everyone the identical
// shared_ptr, so a shared_ptr compare is the viewer check) receives a
// ContainerSetSlotPacket with the slot's current content, built with
// THAT viewer's own window id — window ids are per-player, reusing the
// sender's would hit a window the viewer never opened. Without this the
// second viewer keeps the stale content and its next click silently
// overwrites the first viewer's change (the placed item then "returns"
// through the cursor buffer when someone closes the container).
void Player::broadcastChestSlotToViewers(int slot) {
    if (!srv_ || !open_chest_ || slot < 0 ||
        slot >= open_chest_->inventory()->getSize())
        return;
    auto cur = open_chest_->inventory()->getItem(slot);
    for (auto& other : srv_->playersSnapshot()) {
        if (!other || other.get() == this)
            continue;
        if (other->open_chest_ != open_chest_)
            continue;
        auto pk = std::make_shared<network::protocol::ContainerSetSlotPacket>();
        pk->windowid = other->container_window_id_;
        pk->slot = (int16_t)slot;
        pk->hotbarSlot = (int16_t)slot;
        pk->itemId = (int16_t)cur.id;
        pk->itemCount = cur.count;
        pk->itemDamage = (int16_t)cur.meta;
        pk->itemNbt = cur.nbt;
        other->sendPacket(pk);
    }
}

void Player::broadcastFurnaceSlotToViewers(int slot) {
    if (!srv_ || !open_furnace_ || slot < 0 || slot >= open_furnace_->size())
        return;
    auto cur = open_furnace_->getItem(slot);
    for (auto& other : srv_->playersSnapshot()) {
        if (!other || other.get() == this)
            continue;
        if (other->open_furnace_ != open_furnace_)
            continue;
        auto pk = std::make_shared<network::protocol::ContainerSetSlotPacket>();
        pk->windowid = other->container_window_id_;
        pk->slot = (int16_t)slot;
        pk->hotbarSlot = (int16_t)slot;
        pk->itemId = (int16_t)cur.id;
        pk->itemCount = cur.count;
        pk->itemDamage = (int16_t)cur.meta;
        pk->itemNbt = cur.nbt;
        other->sendPacket(pk);
    }
}

void Player::closeOpenChest() {
    // Cubre ambos contenedores (chest or furnace) — only UNO can estar
    // abierto to the time, this way that basta with close that which is seteado.
    if (open_chest_) {
        // InventoryCloseEvent fires for every window teardown (client close,
        // replaced window, disconnect) — chest-UI plugins react here.
        event::EventManager::get().fire<event::inventory::InventoryCloseEvent>(
            std::static_pointer_cast<inventory::Inventory>(open_chest_->inventory()), this);
        auto cp = std::make_shared<network::protocol::ContainerClosePacket>();
        cp->windowid = container_window_id_;
        sendPacket(cp);

        open_chest_->removeViewer();
        // Lid down + close sound, but only for the LAST viewer leaving:
        // with another player still inside the chest stays visibly open.
        if (open_chest_->viewerCount() == 0)
            broadcastChestBlockEvent(this, *open_chest_, 0);
        open_chest_.reset();
    }
    if (open_furnace_) {
        event::EventManager::get().fire<event::inventory::InventoryCloseEvent>(
            std::static_pointer_cast<inventory::Inventory>(open_furnace_->inventory()), this);
        auto cp = std::make_shared<network::protocol::ContainerClosePacket>();
        cp->windowid = container_window_id_;
        sendPacket(cp);

        open_furnace_->removeViewer(this);
        open_furnace_.reset();
    }
    container_window_id_ = 0;
}

// openFurnaceAt — mirror exacto of openChestAt for the furnace:
// TileFurnace persistente via World::getTile/setTile, viewers for the
// progress of the tick, ContainerOpenPacket tipo furnace and dump of
// the 3 slots (input / fuel / output).
void Player::openFurnaceAt(int x, int y, int z) {
    auto* lvl = getWorld();
    if (!lvl)
        return;

    // only a contenedor abierto per player.
    if (open_chest_ || open_furnace_) {
        closeOpenChest();
    }

    std::shared_ptr<tile::TileFurnace> furnace;
    auto existing = lvl->getTile(x, y, z);
    if (existing) {
        furnace = std::dynamic_pointer_cast<tile::TileFurnace>(existing);
        if (!furnace)
            return; // other tipo of tile in that position
    } else {
        furnace = std::make_shared<tile::TileFurnace>(
            cursestone::world::Position((double)x, (double)y, (double)z));
        lvl->setTile(x, y, z, furnace);
    }

    // InventoryOpenEvent — cancelable (protección of zona, etc.), igual
    // that in openChestAt(). The shared_ptr is owning: listeners may outlive
    // this call and must never dangle on the tile's inventory.
    auto ev = event::EventManager::get().fire<event::inventory::InventoryOpenEvent>(
        std::static_pointer_cast<inventory::Inventory>(furnace->inventory()), this);
    if (ev.isCancelled())
        return;

    furnace->addViewer(this);
    open_furnace_ = furnace;
    openContainerWindow(); // sets container_window_id_ for every packet below

    // BlockEntityData BEFORE the open record — the furnace screen binds
    // to the block actor at these coords and dies when it is missing
    // (a village/structure furnace is just a block until a tile exists,
    // and a chunk streamed before the tile was created never carried
    // the NBT). Same order the reference produces: tile spawn, then
    // ContainerOpen, then the content dump.
    sendBlockEntityData(x, y, z, *furnace, nullptr);

    auto op = std::make_shared<network::protocol::ContainerOpenPacket>();
    op->windowid = container_window_id_;
    op->type = network::protocol::ContainerOpenPacket::TYPE_FURNACE;
    op->slots = 3;
    op->x = x;
    op->y = y;
    op->z = z;
    sendPacket(op);

    // dump complete of the 3 slots — same motivo that the chest:
    // BaseInventory::sendContents() hardcodea windowid=0.
    auto content = std::make_shared<network::protocol::ContainerSetContentPacket>();
    content->windowid = container_window_id_;
    content->slots.reserve(3);
    for (int i = 0; i < 3; ++i)
        content->slots.push_back(furnace->getItem(i));
    sendPacket(content);

    // NO immediate ContainerSetData here: the reference server pushes
    // furnace progress only from the tile update tick, in a batch the
    // client processes after the window is fully open. Firing the
    // property packets inside the same open batch is off-reference and
    // the tile tick delivers fresh values one tick later anyway.

    if (srv_ && srv_->advancedLog()) {
        utils::MainLogger::get().info("[FURNACE] " + cfg_.username + " opened furnace at " +
                                      std::to_string(x) + "," + std::to_string(y) + "," +
                                      std::to_string(z));
    }
}

// ============================================================
// openContainerWindow — assign the next window id to the container this
// player just opened and return it.
//
// The reference server hands container ids out from 2 upward in to cycle
// (player::addWindow: max(2, ++windowCnt % 99)), because 0 is the
// client's own inventory and 0x78/0x79/0x7a are armor/creative/hotbar.
// We copy that range exactly: the old fixed id 1 is never sent by the
// reference server and the client can refuse to open to window it does
// not recognise.
// ============================================================
uint8_t Player::openContainerWindow() {
    container_window_id_ = next_container_window_id_;
    next_container_window_id_ =
        (next_container_window_id_ >= 98) ? 2 : (uint8_t)(next_container_window_id_ + 1);
    return container_window_id_;
}

// ============================================================
// handleSignEdit — portado of Genisys player::handleDataPacket case
// BLOCK_ENTITY_DATA_PACKET (protocolo 84): editing of sign.
//   - descarta ediciones to more of 100 blocks;
//   - requires that the tile is a Sign and that the NBT "id" is "Sign";
//   - only the Creator (UUID of the player) can edit;
//   - dispara SignChangeEvent (cancelable) and applies the líneas.
// ============================================================
void Player::handleSignEdit(network::protocol::BlockEntityDataPacket& pk) {
    if (!spawned() || !srv_)
        return;
    world::World* lvl = getWorld();
    if (!lvl)
        return;

    double dx = (double)pk.x - pos_.x;
    double dy = (double)pk.y - pos_.y;
    double dz = (double)pk.z - pos_.z;
    if (dx * dx + dy * dy + dz * dz > 10000.0)
        return;

    auto tile = lvl->getTile(pk.x, pk.y, pk.z);
    auto sign = std::dynamic_pointer_cast<tile::TileSign>(tile);
    if (!sign)
        return;
    if (pk.namedtag.empty())
        return;

    utils::TagGuard root(mcp_nbt_read_bytes(pk.namedtag.data(), pk.namedtag.size(),
                                            /*little_endian=*/true));
    if (!root)
        return;

    std::string id = mcp_compound_tag_get_string(root.get(), "id", "");
    if (id != "Sign")
        return;

    std::array<std::string, 4> lines = {mcp_compound_tag_get_string(root.get(), "Text1", ""),
                                        mcp_compound_tag_get_string(root.get(), "Text2", ""),
                                        mcp_compound_tag_get_string(root.get(), "Text3", ""),
                                        mcp_compound_tag_get_string(root.get(), "Text4", "")};

    // Genisys compares the Creator contra the NBT of the tile of the server
    // (the sign's stored creator), not contra the packet — the client never
    // takes precedence Creator in its BlockEntityData, this way that leerlo of the packet haría
    // that all editing is cancelara (bug). the dueño is who colocó the
    // sign (player::handleBlockPlace setea the Creator of the tile).
    auto ev = event::EventManager::get().fire<event::block::SignChangeEvent>(pk.x, pk.y, pk.z, this,
                                                                             lines);
    const std::string& creator = sign->creator();
    if (creator.empty() || creator != cfg_.uuid)
        ev.setCancelled(true);
    if (ev.isCancelled()) {
        // Genisys: spawnTo(player) — forwards the NBT current of the sign
        // to the editor for revertir the text that the client already mostró.
        sendBlockEntityData(pk.x, pk.y, pk.z, *sign, this);
        return;
    }

    for (int i = 0; i < 4; ++i)
        sign->setLine(i, ev.getLine(i));
    // Genisys: Sign::setText() termina in spawnToAll() — the sign
    // is respawns to all its viewers so that vean the text new without
    // recargar the chunk.
    sendBlockEntityData(pk.x, pk.y, pk.z, *sign, nullptr);
}

// ============================================================
// sendBlockEntityData — serializa the NBT of a tile and it takes precedence as
// BlockEntityDataPacket. `only` selecciona a only destinatario
// (Genisys tile::spawnTo), or all those that trackean the chunk
// (Genisys tile::spawnToAll). Reutilizable by any block entity.
// ============================================================
void Player::sendBlockEntityData(int32_t x, int32_t y, int32_t z, tile::Tile& t, Player* only) {
    if (!srv_)
        return;

    utils::TagGuard tag(mcp_compound_tag_new());
    if (!tag)
        return;
    t.writeNBT(tag.get());
    size_t blen = 0;
    utils::BufferPtr bbuf(mcp_nbt_write_bytes(tag.get(), /*little_endian=*/true, &blen));
    if (!bbuf || blen == 0)
        return;

    auto pk = std::make_shared<network::protocol::BlockEntityDataPacket>();
    pk->x = x;
    pk->y = y;
    pk->z = z;
    pk->namedtag.assign(bbuf.get(), bbuf.get() + blen);

    if (only) {
        only->sendPacket(pk);
        return;
    }

    int32_t cx = x >> 4;
    int32_t cz = z >> 4;
    for (auto& other : srv_->playersSnapshot()) {
        if (!other)
            continue;
        if (other->getWorld() != getWorld())
            continue; // same world
        if (other.get() == this || other->canSeeBlock(cx, cz)) {
            other->sendPacket(pk);
        }
    }
}
} // namespace cursestone
