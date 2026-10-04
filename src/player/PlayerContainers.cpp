#include "Player.h"
#include "world/World.h"
#include "world/Position.h"
#include "network/protocol/BlockEntityDataPacket.h"
#include "network/protocol/ContainerOpenPacket.h"
#include "network/protocol/ContainerClosePacket.h"
#include "network/protocol/ContainerSetContentPacket.h"
#include "tile/Tile.h"
#include "tile/TileChest.h"
#include "tile/TileFurnace.h"
#include "tile/TileSign.h"
#include "inventory/FurnaceInventory.h"
#include "inventory/Inventory.h"
#include "event/EventManager.h"
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
// openChestAt / closeOpenChest — the real chest flow.
//
// TileChest is the single source of truth for a chest's contents
// (see TileChest.h). World::getTile/setTile is what makes the SAME
// TileChest instance come back every time this position is opened,
// instead of a fresh empty one — so items survive a close+reopen for
// as long as the process keeps running (no disk persistence yet, see
// the comment on World::getTile in World.h).
// ============================================================
void Player::openChestAt(int x, int y, int z) {
    if (!srv_ || !srv_->getDefaultWorld()) return;
    auto* lvl = srv_->getDefaultWorld();

    // Only one container open at a time per player (see openContainerWindow).
    if (open_chest_) {
        closeOpenChest();
    }

    // Reuse the existing TileChest at this position, or create one the
    // first time this chest is opened. World::getTile returns a
    // shared_ptr<tile::Tile> (the base type — the registry can hold any
    // tile kind), so it needs a dynamic_cast back down to TileChest.
    // A null result after a non-null getTile() would mean a different
    // kind of tile-entity is registered at this position, which
    // shouldn't happen for a block we've just confirmed is block::CHEST,
    // but we guard it anyway rather than dereferencing a null pointer.
    std::shared_ptr<tile::TileChest> chest;
    auto existing = lvl->getTile(x, y, z);
    if (existing) {
        chest = std::dynamic_pointer_cast<tile::TileChest>(existing);
        if (!chest) return;
    } else {
        chest = std::make_shared<tile::TileChest>(
            cursestone::world::Position((double)x, (double)y, (double)z));
        lvl->setTile(x, y, z, chest);
    }

    // InventoryOpenEvent — a listener can cancel (e.g. zone protection,
    // a chest mid-trade) to block opening even though PlayerInteractEvent
    // already passed. Nothing is sent to the client and no viewer is
    // registered if this is cancelled.
    auto ev = event::EventManager::get().fire<event::inventory::InventoryOpenEvent>(
        std::static_pointer_cast<inventory::Inventory>(chest->inventory()), this);
    if (ev.isCancelled()) return;

    chest->addViewer();
    open_chest_ = chest;
    openContainerWindow();   // sets container_window_id_ for every packet below

    auto op = std::make_shared<network::protocol::ContainerOpenPacket>();
    op->windowid = container_window_id_;
    op->type     = network::protocol::ContainerOpenPacket::TYPE_CONTAINER;
    op->slots    = (int16_t)chest->inventory()->getSize();
    op->x = x; op->y = y; op->z = z;
    op->entityId = -1;  // block-based container, not entity-based
    sendPacket(op);

    // The client can only draw and open a chest THROUGH its block
    // actor: if the chunk went out before the tile existed (a chest
    // from a village or a structure is just a block until somebody
    // asks for it), the client has no block entity at these
    // coordinates, shows an unknown-but-solid block and refuses to
    // open the UI. Send it now — to everyone looking at the chunk, not
    // just the opener. World::ensureTilesLoaded covers the normal
    // path (tiles exist before the chunk is sent); this covers the
    // ones created after the fact.
    sendBlockEntityData(x, y, z, *chest, nullptr);

    // Full slot dump so the client actually shows the contents. Can't
    // reuse BaseInventory::sendContents() — it hardcodes windowid=0
    // (correct for the player's own inventory, wrong here: it would
    // collide with SPECIAL_INVENTORY and get treated as a hotbar/main
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
            std::to_string(x) + "," + std::to_string(y) + "," + std::to_string(z));
    }
}

void Player::closeOpenChest() {
    // Cubre ambos contenedores (cofre o horno) — solo UNO puede estar
    // abierto a la vez, así que basta con cerrar el que esté seteado.
    if (open_chest_) {
        auto cp = std::make_shared<network::protocol::ContainerClosePacket>();
        cp->windowid = container_window_id_;
        sendPacket(cp);

        open_chest_->removeViewer();
        open_chest_.reset();
    }
    if (open_furnace_) {
        auto cp = std::make_shared<network::protocol::ContainerClosePacket>();
        cp->windowid = container_window_id_;
        sendPacket(cp);

        open_furnace_->removeViewer(this);
        open_furnace_.reset();
    }
    container_window_id_ = 0;
}

// openFurnaceAt — espejo exacto de openChestAt para el horno:
// TileFurnace persistente via World::getTile/setTile, viewers para el
// progreso del tick, ContainerOpenPacket tipo FURNACE y volcado de
// los 3 slots (input / fuel / output).
void Player::openFurnaceAt(int x, int y, int z) {
    if (!srv_ || !srv_->getDefaultWorld()) return;
    auto* lvl = srv_->getDefaultWorld();

    // Solo un contenedor abierto por jugador.
    if (open_chest_ || open_furnace_) {
        closeOpenChest();
    }

    std::shared_ptr<tile::TileFurnace> furnace;
    auto existing = lvl->getTile(x, y, z);
    if (existing) {
        furnace = std::dynamic_pointer_cast<tile::TileFurnace>(existing);
        if (!furnace) return;   // otro tipo de tile en esa posición
    } else {
        furnace = std::make_shared<tile::TileFurnace>(
            cursestone::world::Position((double)x, (double)y, (double)z));
        lvl->setTile(x, y, z, furnace);
    }

    // InventoryOpenEvent — cancelable (protección de zona, etc.), igual
    // que en openChestAt().
    inventory::Inventory* fInvRaw =
        static_cast<inventory::Inventory*>(furnace->inventory().get());
    auto ev = event::EventManager::get().fire<event::inventory::InventoryOpenEvent>(
        std::shared_ptr<inventory::Inventory>(std::shared_ptr<void>(), fInvRaw), this);
    if (ev.isCancelled()) return;

    furnace->addViewer(this);
    open_furnace_ = furnace;
    openContainerWindow();  // sets container_window_id_ for every packet below

    auto op = std::make_shared<network::protocol::ContainerOpenPacket>();
    op->windowid = container_window_id_;
    op->type     = network::protocol::ContainerOpenPacket::TYPE_FURNACE;
    op->slots    = 3;
    op->x = x; op->y = y; op->z = z;
    op->entityId = -1;
    sendPacket(op);

    // Mismo motivo que en el cofre: sin block entity el cliente 0.15.x
    // no tiene sobre qué montar la UI del horno (y abrirla sin él lo
    // revienta), asi que se le manda antes de cualquier otro paquete
    // de la ventana.
    sendBlockEntityData(x, y, z, *furnace, nullptr);

    // Volcado completo de los 3 slots — mismo motivo que el cofre:
    // BaseInventory::sendContents() hardcodea windowid=0.
    auto content = std::make_shared<network::protocol::ContainerSetContentPacket>();
    content->windowid = container_window_id_;
    content->slots.reserve(3);
    for (int i = 0; i < 3; ++i) content->slots.push_back(furnace->getItem(i));
    sendPacket(content);

    // Estado inicial de flecha/llama para que la UI no arranque en un
    // estado fantasma (ContainerSetDataPacket property 0/1).
    furnace->sendProgressToViewers();

    if (srv_ && srv_->advancedLog()) {
        utils::MainLogger::get().info("[FURNACE] " + cfg_.username + " opened furnace at " +
            std::to_string(x) + "," + std::to_string(y) + "," + std::to_string(z));
    }
}

// ============================================================
// openContainerWindow — assign the next window id to the container this
// player just opened and return it.
//
// Infinity hands container ids out from 2 upward in a cycle
// (Player::addWindow: max(2, ++windowCnt % 99)), because 0 is the
// client's own inventory and 0x78/0x79/0x7a are armor/creative/hotbar.
// We copy that range exactly: the old fixed id 1 is never sent by the
// reference server and the client can refuse to open a window it does
// not recognise.
// ============================================================
uint8_t Player::openContainerWindow() {
    container_window_id_ = next_container_window_id_;
    next_container_window_id_ =
        (next_container_window_id_ >= 98) ? 2
                                           : (uint8_t)(next_container_window_id_ + 1);
    return container_window_id_;
}

// ============================================================
// handleSignEdit — portado de Genisys Player::handleDataPacket case
// BLOCK_ENTITY_DATA_PACKET (protocolo 84): edición de cartel.
//   - descarta ediciones a más de 100 bloques;
//   - exige que el tile sea un Sign y que el NBT "id" sea "Sign";
//   - sólo el Creator (UUID del jugador) puede editar;
//   - dispara SignChangeEvent (cancelable) y aplica las líneas.
// ============================================================
void Player::handleSignEdit(network::protocol::BlockEntityDataPacket& pk) {
    if (!spawned() || !srv_) return;
    world::World* lvl = srv_->getDefaultWorld();
    if (!lvl) return;

    double dx = (double)pk.x - pos_.x;
    double dy = (double)pk.y - pos_.y;
    double dz = (double)pk.z - pos_.z;
    if (dx * dx + dy * dy + dz * dz > 10000.0) return;

    auto tile = lvl->getTile(pk.x, pk.y, pk.z);
    auto sign = std::dynamic_pointer_cast<tile::TileSign>(tile);
    if (!sign) return;
    if (pk.namedtag.empty()) return;

    mcp_tag_t* root = mcp_nbt_read_bytes(pk.namedtag.data(), pk.namedtag.size(),
                                         /*little_endian=*/true);
    if (!root) return;

    std::string id = mcp_compound_tag_get_string(root, "id", "");
    if (id != "Sign") { mcp_tag_destroy(root); return; }

    std::array<std::string, 4> lines = {
        mcp_compound_tag_get_string(root, "Text1", ""),
        mcp_compound_tag_get_string(root, "Text2", ""),
        mcp_compound_tag_get_string(root, "Text3", ""),
        mcp_compound_tag_get_string(root, "Text4", "")
    };
    mcp_tag_destroy(root);

    // Genisys compara el Creator contra el NBT DEL TILE del servidor
    // (`$t->namedtag["Creator"]`), no contra el paquete — el cliente NUNCA
    // manda Creator en su BlockEntityData, así que leerlo del paquete haría
    // que toda edición se cancelara (bug). El dueño es quien colocó el
    // cartel (Player::handleBlockPlace setea el Creator del tile).
    auto ev = event::EventManager::get().fire<event::block::SignChangeEvent>(
        pk.x, pk.y, pk.z, this, lines);
    const std::string& creator = sign->creator();
    if (creator.empty() || creator != cfg_.uuid) ev.setCancelled(true);
    if (ev.isCancelled()) {
        // Genisys: `$t->spawnTo($this)` — reenvía el NBT actual del cartel
        // al editor para revertir el texto que el cliente ya mostró.
        sendBlockEntityData(pk.x, pk.y, pk.z, *sign, this);
        return;
    }

    for (int i = 0; i < 4; ++i) sign->setLine(i, ev.getLine(i));
    // Genisys: Sign::setText() termina en `$this->spawnToAll()` — el cartel
    // se re-spawnea a todos sus viewers para que vean el texto nuevo sin
    // recargar el chunk.
    sendBlockEntityData(pk.x, pk.y, pk.z, *sign, nullptr);
}

// ============================================================
// sendBlockEntityData — serializa el NBT de un tile y lo manda como
// BlockEntityDataPacket. `only` selecciona un único destinatario
// (Genisys Tile::spawnTo), o todos los que trackean el chunk
// (Genisys Tile::spawnToAll). Reutilizable por cualquier block entity.
// ============================================================
void Player::sendBlockEntityData(int32_t x, int32_t y, int32_t z,
                                 tile::Tile& t, Player* only) {
    if (!srv_) return;

    mcp_tag_t* tag = mcp_compound_tag_new();
    if (!tag) return;
    t.writeNBT(tag);
    size_t blen = 0;
    uint8_t* bbuf = mcp_nbt_write_bytes(tag, /*little_endian=*/true, &blen);
    mcp_tag_destroy(tag);
    if (!bbuf || blen == 0) { free(bbuf); return; }

    auto pk = std::make_shared<network::protocol::BlockEntityDataPacket>();
    pk->x = x;
    pk->y = y;
    pk->z = z;
    pk->namedtag.assign(bbuf, bbuf + blen);
    free(bbuf);

    if (only) {
        only->sendPacket(pk);
        return;
    }

    int32_t cx = x >> 4;
    int32_t cz = z >> 4;
    for (auto& other : srv_->playersSnapshot()) {
        if (!other) continue;
        if (other.get() == this || other->canSeeBlock(cx, cz)) {
            other->sendPacket(pk);
        }
    }
}} // namespace cursestone
