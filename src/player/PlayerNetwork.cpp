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
#include "item/Item.h"
#include "workbench/WorkbenchManager.h"
#include "diagnostics/Diagnostics.h"
#include "network/protocol/Info.h"
#include "network/protocol/DataPacket.h"
#include "network/protocol/BatchPacket.h"
#include "network/protocol/TextPacket.h"
#include "network/protocol/LoginPacket.h"
#include "network/protocol/PlayStatusPacket.h"
#include "network/protocol/MovePlayerPacket.h"
#include "network/protocol/RequestChunkRadiusPacket.h"
#include "network/protocol/ChunkRadiusUpdatedPacket.h"
#include "network/protocol/PlayerActionPacket.h"
#include "network/protocol/UseItemPacket.h"
#include "network/protocol/RemoveBlockPacket.h"
#include "network/protocol/MobEquipmentPacket.h"
#include "network/protocol/AnimatePacket.h"
#include "network/protocol/ContainerSetSlotPacket.h"
#include "network/protocol/ContainerClosePacket.h"
#include "network/protocol/CraftingEventPacket.h"
#include "network/protocol/InteractPacket.h"
#include "network/protocol/EntityEventPacket.h"
#include "network/protocol/DropItemPacket.h"
#include "network/protocol/BlockEntityDataPacket.h"
#include "network/protocol/ItemFrameDropItemPacket.h"
#include "network/protocol/AdventureSettingsPacket.h"
#include "tile/TileChest.h"
#include "tile/TileFurnace.h"
#include "inventory/CreativeItems.h"
#include "inventory/FurnaceInventory.h"
#include "inventory/PlayerInventory.h"
#include "event/EventManager.h"
#include "event/inventory/InventoryClickEvent.h"
#include "event/player/PlayerItemHeldEvent.h"
#include "utils/MainLogger.hpp"
#include <chrono>
#include <cstdio>

namespace cursestone {

namespace {

// Sends all subpackets in one compressed BatchPacket using the specified channel.
void sendBatchWire(cursestone::network::AdvancedSourceInterface* iface, const std::string& ip,
                   uint16_t port, std::vector<std::vector<uint8_t>>& subs, bool ordered) {
    if (!iface || subs.empty())
        return;

    network::protocol::BatchPacket batch;
    for (auto& sub : subs)
        batch.addSubPacket(sub);

    std::vector<uint8_t> batchBody;
    batch.encode(batchBody);
    if (batchBody.empty())
        return;

    // MCPE game packets must start with 0xFE, as required by the reference server's RakLibInterface.
    std::vector<uint8_t> wire;
    wire.push_back(0xFE);                                        // Game packet marker
    wire.push_back(cursestone::network::protocol::BATCH_PACKET); // 0x06
    wire.insert(wire.end(), batchBody.begin(), batchBody.end());

    iface->sendRawPacket(ip, port, wire, ordered);
}

} // namespace

void Player::sendPacket(std::shared_ptr<network::protocol::DataPacket> p, bool ordered) {
    if (!p)
        return;
    if (!connected_.load(std::memory_order_relaxed))
        return;
    if (!adv_iface_) {
        utils::MainLogger::get().warn("sendPacket: no interface for " + cfg_.username);
        return;
    }

    // Record the outgoing packet in Workbench.
    WorkbenchManager::get().onPacketOut(cfg_.username, p->pid(), 0);

    // Serialize the packet as [1-byte ID][body].
    std::vector<uint8_t> body;
    p->writePacket(body);
    if (body.empty())
        return;

    // Record recent outgoing packets for diagnostics.
    diagnostics::Diagnostics::get().recordPacket(
        /*out=*/true, cfg_.username, p->pid(), body.size());

    bool flush_now = false;
    {
        std::lock_guard<std::mutex> lk(batch_mtx_);
        auto& queue = ordered ? batch_ordered_ : batch_unordered_;
        batch_bytes_ += body.size();
        queue.push_back(std::move(body));
        flush_now = batch_bytes_ >= BATCH_FLUSH_BYTES;
    }
    // Flush immediately when the batch threshold is reached, outside the buffer lock.
    if (flush_now)
        flushPendingPackets();
}

void Player::flushPendingPackets() {
    if (!connected_.load(std::memory_order_relaxed))
        return;
    if (!adv_iface_)
        return;

    std::vector<std::vector<uint8_t>> take_unordered;
    std::vector<std::vector<uint8_t>> take_ordered;
    {
        std::lock_guard<std::mutex> lk(batch_mtx_);
        if (batch_unordered_.empty() && batch_ordered_.empty())
            return;
        take_unordered.swap(batch_unordered_);
        take_ordered.swap(batch_ordered_);
        batch_bytes_ = 0;
    }
    // Send ordered packets first to preserve the login handshake before chunk data.
    sendBatchWire(adv_iface_, cfg_.ip, cfg_.port, take_ordered, /*ordered=*/true);
    sendBatchWire(adv_iface_, cfg_.ip, cfg_.port, take_unordered, /*ordered=*/false);
}

void Player::sendMessage(const std::string& msg) {
    auto pk = std::make_shared<network::protocol::TextPacket>();
    pk->type = network::protocol::TextPacket::TYPE_RAW;
    pk->message = msg;
    sendPacket(pk);
}

// Gives a creative item and echoes the client's original slot fields for confirmation.
static void giveCreativeItem(cursestone::Player* player,
                             std::shared_ptr<cursestone::inventory::PlayerInventory> inv,
                             const network::protocol::ContainerSetSlotPacket& cs,
                             cursestone::item::ItemStack it) {
    if (!player || !inv)
        return;
    // Resolve missing item data from the creative catalog using the client's slot index.
    if (it.id == 0) {
        const auto& catalog = cursestone::inventory::getCreativeItems();
        if (cs.slot >= 0 && (size_t)cs.slot < catalog.size()) {
            it = catalog[(size_t)cs.slot];
        }
    }
    if (it.id == 0)
        return;
    auto leftover = inv->addItem({it});
    bool given = leftover.empty() || leftover[0].count < it.count;

    auto confirm = std::make_shared<network::protocol::ContainerSetSlotPacket>();
    confirm->windowid = cs.windowid;
    confirm->slot = cs.slot;
    confirm->hotbarSlot = cs.hotbarSlot;
    if (given) {
        confirm->itemId = (int16_t)it.id;
        confirm->itemCount = it.count;
        confirm->itemDamage = (int16_t)it.meta;
    } else {
        confirm->itemId = 0; // Main inventory is full.
    }
    player->sendPacket(confirm);
}

// Handles incoming packets by packet ID.
void Player::handleDataPacket(network::protocol::DataPacket& p) {
    if (!connected_.load(std::memory_order_relaxed))
        return;
    uint8_t pid = p.pid();

    // Update activity time and record the incoming packet in Workbench.
    last_activity_ms_ = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch())
                            .count();
    WorkbenchManager::get().onPacketIn(cfg_.ip + ":" + std::to_string(cfg_.port), pid, 0);

    // Record the incoming packet for diagnostics.
    diagnostics::Diagnostics::get().recordPacket(
        /*out=*/false, cfg_.username, pid, 0);

    // Catch handler errors, generate diagnostics, and disconnect the affected player.
    try {
        switch (pid) {
        case network::protocol::LOGIN_PACKET:
            if (auto* lp = dynamic_cast<network::protocol::LoginPacket*>(&p)) {
                handleLogin(*lp);
            }
            break;
        case network::protocol::PLAY_STATUS_PACKET:
            // Wait for the client's spawn acknowledgment before resending chunks.
            if (auto* ps = dynamic_cast<network::protocol::PlayStatusPacket*>(&p);
                ps && ps->status == network::protocol::PlayStatusPacket::PLAYER_SPAWN) {
                handleClientSpawnAck();
            }
            break;
        case network::protocol::MOVE_PLAYER_PACKET:
            if (auto* mp = dynamic_cast<network::protocol::MovePlayerPacket*>(&p)) {
                handleMovePlayer(mp->x, mp->y, mp->z, mp->yaw, mp->pitch, mp->onGround);
            }
            break;
        case network::protocol::TEXT_PACKET:
            if (auto* tp = dynamic_cast<network::protocol::TextPacket*>(&p)) {
                if (tp->type == network::protocol::TextPacket::TYPE_CHAT) {
                    handleChat(tp->message);
                }
            }
            break;
        case network::protocol::REQUEST_CHUNK_RADIUS_PACKET:
            // Clamp the requested view distance to the server's configured maximum.
            if (auto* rp = dynamic_cast<network::protocol::RequestChunkRadiusPacket*>(&p)) {
                int32_t r = rp->radius;
                if (r < 1)
                    r = 1;
                if (r > cfg_.view_distance)
                    r = cfg_.view_distance;
                client_chunk_radius_.store(r, std::memory_order_relaxed);
                auto resp = std::make_shared<network::protocol::ChunkRadiusUpdatedPacket>();
                resp->radius = r;
                sendPacket(resp);

                // The client applies the new radius immediately and discards
                // every stored chunk beyond it, while this side keeps marking
                // them as sent. A later radius increase would then find
                // nothing to re-stream and the terrain would stay invisible
                // until relogin. Drop the out-of-radius chunks here so the
                // streaming loop in onUpdate() re-sends them when the radius
                // comes back.
                int32_t pcx = (int32_t)pos_.x >> 4;
                int32_t pcz = (int32_t)pos_.z >> 4;
                {
                    std::lock_guard<std::mutex> lk(chunks_mtx_);
                    for (auto it = used_chunks_.begin(); it != used_chunks_.end();) {
                        int64_t h = *it;
                        int32_t dx = (int32_t)(h >> 32) - pcx;
                        int32_t dz = (int32_t)(h & 0xffffffffLL) - pcz;
                        if (dx < 0)
                            dx = -dx;
                        if (dz < 0)
                            dz = -dz;
                        if (dx > r || dz > r)
                            it = used_chunks_.erase(it);
                        else
                            ++it;
                    }
                }
            }
            break;
        case network::protocol::PLAYER_ACTION_PACKET:
            if (auto* pa = dynamic_cast<network::protocol::PlayerActionPacket*>(&p)) {
                handlePlayerAction(pa->action, pa->x, pa->y, pa->z, pa->face);
            }
            break;
        case network::protocol::USE_ITEM_PACKET:
            if (auto* ui = dynamic_cast<network::protocol::UseItemPacket*>(&p)) {
                // Pass the full held item ID, count, and damage to the block-placement handler.
                handleBlockPlace(ui->x, ui->y, ui->z, ui->face, ui->itemId, ui->itemCount,
                                 ui->itemDamage, ui->fx, ui->fy, ui->fz);
            }
            break;
        case network::protocol::REMOVE_BLOCK_PACKET:
            if (auto* rb = dynamic_cast<network::protocol::RemoveBlockPacket*>(&p)) {
                handleBlockBreak(rb->x, rb->y, rb->z);
            }
            break;
        case network::protocol::MOB_EQUIPMENT_PACKET:
            // The client reports its held hotbar slot; inventory-screen packets may use a different slot field.
            if (auto* me = dynamic_cast<network::protocol::MobEquipmentPacket*>(&p)) {
                if (inv_) {
                    // Wire byte order (Genisys 0.15 / the reference server):
                    // byte 1 (hotbarSlot) is the PADDED ABSOLUTE inventory
                    // slot the position now links to (position + 9; 255 =
                    // blank selection), byte 2 (selectedSlot) is the hotbar
                    // position 0..8. The reference applies the link remap
                    // exactly here: setHeldItemIndex(selectedSlot, false,
                    // slot) — "the only time and place hotbar mapping should
                    // ever be changed".
                    int selected = (int)me->selectedSlot;
                    if (selected > 8)
                        selected = (int)me->hotbarSlot;
                    if (selected >= 0 && selected <= 8) {
                        int mapping =
                            (int)me->hotbarSlot - inventory::PLAYER_INV_HOTBAR_SIZE;
                        if (mapping < 0 || mapping >= inventory::PlayerInventory::SIZE)
                            mapping = -1; // blank/invalid: unlink the position
                        // A creative take re-sends MobEquipment with the SAME
                        // position only to push the new link, so the remap
                        // must also fire when the link alone changed.
                        bool positionChanged = selected != inv_->getHeldItemIndex();
                        bool linkChanged = mapping != inv_->getHotbarSlotIndex(selected);
                        if (positionChanged || linkChanged) {
                            // Plugins see the item the player will hold AFTER
                            // the remap (the reference resolves it from the
                            // incoming mapping before applying the change).
                            item::ItemStack heldAfter;
                            if (mapping >= 0)
                                heldAfter = inv_->getItem(mapping);
                            // Plugins can cancel the hotbar change and trigger an inventory resync.
                            auto ev =
                                event::EventManager::get().fire<event::player::PlayerItemHeldEvent>(
                                    this, heldAfter, selected, (int)me->hotbarSlot);
                            if (ev.isCancelled()) {
                                sendInventory();
                            } else {
                                inv_->setHeldItemIndex(selected, mapping);
                                // Switching items cancels an in-progress bow pose for
                                // viewers (the reference clears DATA_FLAG_ACTION on equip).
                                if (positionChanged && bow_pose_active_)
                                    broadcastBowPose(false);
                                // Remote clients only render the hand item from
                                // MobEquipmentPacket — re-broadcast it after every equip.
                                broadcastHeldItemToViewers();
                            }
                        }
                        if (srv_ && srv_->advancedLog()) {
                            utils::MainLogger::get().info(
                                "[INV] " + cfg_.username + " switched to hotbar slot " +
                                std::to_string(inv_->getHeldItemIndex()) +
                                " (inventory slot " +
                                std::to_string(inv_->heldSlot()) + ")");
                        }
                    }
                }
            }
            break;
        case network::protocol::ANIMATE_PACKET:
            // Broadcast arm swings to nearby players, excluding the player who performed them.
            if (auto* an = dynamic_cast<network::protocol::AnimatePacket*>(&p)) {
                an->eid = runtimeId();
                auto fwd = std::make_shared<network::protocol::AnimatePacket>(*an);
                if (srv_) {
                    const auto& pos = position();
                    int32_t an_cx = (int32_t)pos.x >> 4;
                    int32_t an_cz = (int32_t)pos.z >> 4;
                    for (auto& other : srv_->playersSnapshot()) {
                        if (!other || other.get() == this)
                            continue;
                        if (other->isPendingRemoval() || !other->spawned())
                            continue;
                        if (!other->isTrackingChunk(an_cx, an_cz))
                            continue;
                        other->sendPacket(fwd);
                    }
                }
            }
            break;
        case network::protocol::MOB_ARMOR_EQUIPMENT_PACKET:
            // Armor updates arrive through ContainerSetSlotPacket with window ID 0x78.
            break;
        case network::protocol::CONTAINER_SET_SLOT_PACKET:
            // Handle player inventory, armor, hotbar, creative, chest, and furnace slot updates.
            if (auto* cs = dynamic_cast<network::protocol::ContainerSetSlotPacket*>(&p)) {
                if (inv_) {
                    cursestone::item::ItemStack it;
                    it.id = static_cast<uint16_t>(cs->itemId);
                    it.count = cs->itemCount;
                    it.meta = static_cast<uint8_t>(cs->itemDamage & 0xFF);
                    // Preserve item NBT so custom names survive inventory updates.
                    it.nbt = cs->itemNbt;

                    // Default valid creative items with a zero count to one.
                    if (it.id != 0 && it.count == 0) {
                        it.count = 1;
                    }

                    // Clamp every client-sent stack to the item's own max
                    // stack size. A client that picked up a legacy oversized
                    // drop (two bows fused into count=2 before the max-stack
                    // fix) would otherwise write the illegal stack straight
                    // into the server's inventory — the state that made the
                    // bow invisible to viewers and unshootable for the holder.
                    int raw_count = it.count;
                    if (it.id != 0) {
                        const item::Item* itDef = item::Item::get(it.id);
                        int itemMax = itDef ? itDef->maxStackSize() : 64;
                        if (itemMax < 1)
                            itemMax = 1;
                        if (it.count > itemMax)
                            it.count = itemMax;
                    }

                    // Log inventory slot updates when advanced logging is enabled.
                    if (srv_ && srv_->advancedLog()) {
                        utils::MainLogger::get().info("[INV] " + cfg_.username +
                                                      " ContainerSetSlot: wid=0x" + ([](uint8_t v) {
                                                          char b[4];
                                                          snprintf(b, 4, "%02x", v);
                                                          return std::string(b);
                                                      })(cs->windowid) +
                                                      " slot=" + std::to_string(cs->slot) +
                                                      " hotbar=" + std::to_string(cs->hotbarSlot) +
                                                      " id=" + std::to_string(it.id) +
                                                      " count=" + std::to_string((int)it.count));
                    }

                    // Route clicks in the open chest to its inventory and confirm the resulting slot.
                    if (cs->windowid == container_window_id_ && open_chest_) {
                        if (open_chest_ && cs->slot >= 0 &&
                            cs->slot < open_chest_->inventory()->getSize()) {
                            auto chestInv = open_chest_->inventory();
                            auto cev =
                                event::EventManager::get()
                                    .fire<event::inventory::InventoryClickEvent>(
                                        std::static_pointer_cast<inventory::Inventory>(chestInv),
                                        this, (int)cs->slot, it);
                            if (!cev.isCancelled()) {
                                // Validate survival writes against the floating inventory buffer.
                                if (isCreative() ||
                                    beginSurvivalSlotWrite(chestInv.get(), cs->slot, it)) {
                                    chestInv->setItem(cs->slot, it);
                                    // Vanilla co-viewer sync: everyone else with
                                    // this chest open sees the new slot content.
                                    broadcastChestSlotToViewers(cs->slot);
                                }
                            }
                            // Confirm the chest slot even when the event cancels the change.
                            auto confirm =
                                std::make_shared<network::protocol::ContainerSetSlotPacket>();
                            confirm->windowid = container_window_id_;
                            confirm->slot = cs->slot;
                            confirm->hotbarSlot = cs->slot;
                            auto cur = chestInv->getItem(cs->slot);
                            confirm->itemId = (int16_t)cur.id;
                            confirm->itemCount = cur.count;
                            confirm->itemDamage = (int16_t)cur.meta;
                            // Preserve NBT when confirming named item stacks.
                            confirm->itemNbt = cur.nbt;
                            sendPacket(confirm);
                        }
                        break;
                    }

                    // Route furnace clicks to its three slots and confirm the resulting item.
                    if (cs->windowid == container_window_id_ && open_furnace_) {
                        if (open_furnace_ && cs->slot >= 0 && cs->slot < 3) {
                            auto f = open_furnace_;
                            auto cev = event::EventManager::get()
                                           .fire<event::inventory::InventoryClickEvent>(
                                               std::static_pointer_cast<inventory::Inventory>(
                                                   f->inventory()),
                                               this, (int)cs->slot, it);
                            if (!cev.isCancelled()) {
                                // Apply the same floating-buffer validation used for chests.
                                if (isCreative() ||
                                    beginSurvivalSlotWrite(f->inventory().get(), cs->slot, it)) {
                                    f->setItem(cs->slot, it);
                                    // Same co-viewer sync as the chest.
                                    broadcastFurnaceSlotToViewers(cs->slot);
                                }
                            }
                            auto confirm =
                                std::make_shared<network::protocol::ContainerSetSlotPacket>();
                            confirm->windowid = container_window_id_;
                            confirm->slot = cs->slot;
                            confirm->hotbarSlot = cs->slot;
                            auto cur = f->getItem(cs->slot);
                            confirm->itemId = (int16_t)cur.id;
                            confirm->itemCount = cur.count;
                            confirm->itemDamage = (int16_t)cur.meta;
                            // Preserve NBT when confirming named item stacks.
                            confirm->itemNbt = cur.nbt;
                            sendPacket(confirm);
                        }
                        break;
                    }

                    // Fire InventoryClickEvent for player inventory, armor, hotbar, and creative slots.
                    bool clickCancelled = false;
                    {
                        auto ev =
                            event::EventManager::get().fire<event::inventory::InventoryClickEvent>(
                                std::static_pointer_cast<inventory::Inventory>(inv_), this,
                                (int)cs->slot, it);
                        clickCancelled = ev.isCancelled();
                    }
                    if (clickCancelled) {
                        // Resync the affected player inventory slot when a plugin cancels the click.
                        using CSS = network::protocol::ContainerSetSlotPacket;
                        if (cs->windowid == CSS::SPECIAL_ARMOR) {
                            sendSlot(inventory::PlayerInventory::ARMOR_OFFSET + cs->slot);
                        } else if (cs->windowid == CSS::SPECIAL_INVENTORY ||
                                   cs->windowid == CSS::SPECIAL_HOTBAR) {
                            sendSlot(cs->slot);
                        }
                        break;
                    }

                    if (cs->windowid ==
                        network::protocol::ContainerSetSlotPacket::SPECIAL_INVENTORY) {
                        // Handle main inventory slots using the absolute slot index sent by the client.
                        if (cs->slot >= 0 && cs->slot < inventory::PlayerInventory::SIZE) {
                            if ((cfg_.gamemode & 1) != 0) {
                                // Creative mode applies the item directly.
                                inv_->setItem(cs->slot, it);
                                // A creative write into a hotbar position
                                // selects that position so the item appears
                                // in hand. setHeldSlot() would instead re-map
                                // the CURRENT held position's link to the
                                // written slot, silently swapping whatever
                                // the player was holding (bow releases then
                                // saw the arrow stack as the held item).
                                if (it.id != 0 && cs->slot >= 0 && cs->slot < 9) {
                                    inv_->setHeldItemIndex(cs->slot);
                                }
                                // A clamped write (legacy oversized stack)
                                // keeps its overflow instead of deleting it.
                                if (raw_count > it.count && it.id != 0) {
                                    item::ItemStack extra = it;
                                    extra.count = raw_count - it.count;
                                    inv_->addItem({extra});
                                }
                            } else if (beginSurvivalSlotWrite(inv_.get(), cs->slot, it)) {
                                inv_->setItem(cs->slot, it);
                            } else if (inv_->getItem(cs->slot).isEmpty() &&
                                       tryClaimNearbyDrop(it)) {
                                // The client picked a dropped item up locally
                                // before the server's pickup scan allowed it
                                // (2s drop delay) and reported the new slot.
                                // The drop is now claimed, so honor the write
                                // instead of wiping the slot back to empty —
                                // the wipe is what made the client re-pick the
                                // still-visible drop in a loop and restart the
                                // drink/eat animation forever.
                                inv_->setItem(cs->slot, it);
                                // A claimed legacy oversized drop keeps its
                                // overflow bows in separate slots.
                                if (raw_count > it.count && it.id != 0) {
                                    item::ItemStack extra = it;
                                    extra.count = raw_count - it.count;
                                    inv_->addItem({extra});
                                }
                            } else {
                                // Reject invalid writes and resync the affected slot.
                                sendSlot(cs->slot);
                            }
                            // setItem() already sends the slot confirmation through onSlotChange().
                        }
                    } else if (cs->windowid ==
                               network::protocol::ContainerSetSlotPacket::SPECIAL_ARMOR) {
                        // Map armor slots 0–3 to inventory slots 36–39.
                        if (cs->slot >= 0 && cs->slot < 4) {
                            int dest = inventory::PlayerInventory::ARMOR_OFFSET + cs->slot;
                            if ((cfg_.gamemode & 1) != 0) {
                                inv_->setItem(dest, it);
                            } else if (beginSurvivalSlotWrite(inv_.get(), dest, it)) {
                                inv_->setItem(dest, it);
                            } else {
                                sendSlot(dest);
                            }
                        }
                    } else if (cs->windowid ==
                               network::protocol::ContainerSetSlotPacket::SPECIAL_HOTBAR) {
                        // Ignore hotbar-link packets as item writes; actual creative selections use window ID 0x00.
                    } else if (cs->windowid ==
                               network::protocol::ContainerSetSlotPacket::SPECIAL_CREATIVE) {
                        // Keep creative-window handling as a fallback for clients that send this packet.
                        giveCreativeItem(this, inv_, *cs, it);
                    }
                }
            }
            break;
        // Acknowledge these packets without further processing.
        case network::protocol::BATCH_PACKET:
        case network::protocol::PLAYER_INPUT_PACKET:
        // Genisys ignores this health confirmation packet.
        case network::protocol::SET_HEALTH_PACKET:
            break;
        case network::protocol::CONTAINER_CLOSE_PACKET: {
            // Vanilla honors the closing window id: window 0 (the player's
            // own inventory screen) and unknown ids never tear a container
            // window down, so a stale or bogus close cannot rip an open
            // chest/furnace away from a chest-UI flow.
            bool container_open = open_chest_ || open_furnace_;
            uint8_t close_wid = container_window_id_;
            if (auto* cc = dynamic_cast<network::protocol::ContainerClosePacket*>(&p))
                close_wid = cc->windowid;
            bool matched = container_open && close_wid == container_window_id_;
            if (matched) {
                // InventoryCloseEvent + the window close echo to the client.
                closeOpenChest();
            } else if (!container_open) {
                handleInventoryCraft();
            }
            // Return cursor items only when a window actually ended.
            if (matched || !container_open)
                returnFloatingItems();
            break;
        }
        case network::protocol::CRAFTING_EVENT_PACKET: {
            // Validate crafting requests, consume ingredients, and grant the recipe output.
            if (auto* ce = dynamic_cast<network::protocol::CraftingEventPacket*>(&p)) {
                handleCraftingEvent(ce->id, ce->input, ce->output, ce->windowId);
            }
            break;
        }
        case network::protocol::INTERACT_PACKET:
            if (auto* ip = dynamic_cast<network::protocol::InteractPacket*>(&p)) {
                handleInteract(ip->action, ip->target);
            }
            break;
        case network::protocol::ENTITY_EVENT_PACKET:
            // Consume the held item when the spawned player sends the eating event.
            if (auto* ee = dynamic_cast<network::protocol::EntityEventPacket*>(&p)) {
                // The reference clears the ACTION pose on EVERY client
                // entity event, not only on the eat-complete one: any
                // client-side event ends the current bow draw / eating
                // gesture, so the pose must go down for viewers too.
                if (spawned() && bow_pose_active_)
                    broadcastBowPose(false);
                if (spawned() && ee->event == 9 /*Eating*/) {
                    consumeHeldItem();
                }
            }
            break;
        case network::protocol::DROP_ITEM_PACKET:
            if (auto* dp = dynamic_cast<network::protocol::DropItemPacket*>(&p)) {
                handleDropItem(*dp);
            }
            break;
        case network::protocol::BLOCK_ENTITY_DATA_PACKET:
            // Validate sign edits and dispatch the cancellable SignChangeEvent.
            if (auto* be = dynamic_cast<network::protocol::BlockEntityDataPacket*>(&p)) {
                handleSignEdit(*be);
            }
            break;
        case network::protocol::ITEM_FRAME_DROP_ITEM_PACKET:
            // Item frames are not implemented yet, so this packet is recognized but ignored.
            if (auto* ifr = dynamic_cast<network::protocol::ItemFrameDropItemPacket*>(&p)) {
                // Silence the unused-variable warning until item frames are implemented.
                (void)ifr;
            }
            break;
        default: {
            // Log unknown packet IDs at debug level.
            char hex[8];
            std::snprintf(hex, sizeof(hex), "%02x", pid);
            utils::MainLogger::get().debug("Player " + cfg_.username +
                                           " sent unhandled packet id=0x" + hex);
            break;
        }
        }
    } catch (const std::exception& e) {
        handlePacketError(pid, std::string("std::exception: ") + e.what());
    } catch (...) {
        handlePacketError(pid, "unknown exception (non-std) while handling packet");
    }
}

// Logs packet-handler exceptions, writes a diagnostic report, and disconnects the affected player.
void Player::handlePacketError(uint8_t pid, const std::string& what) {
    char hex[8];
    std::snprintf(hex, sizeof(hex), "%02x", pid);
    const std::string summary = "Internal server error handling packet 0x" + std::string(hex) +
                                " from " + cfg_.username + ": " + what;

    // Prevent diagnostics failures from escaping the exception handler.
    try {
        diagnostics::Diagnostics::get().writeReport(
            "exception", summary, "class=Player::handleDataPacket pid=0x" + std::string(hex));
    } catch (...) {}

    // Show the error details to the player before disconnecting.
    try {
        kick("Internal server error",
             "The server hit an error while processing your action and the "
             "error was logged. Details: " +
                 what);
    } catch (...) {
        close("server error");
    }
}

void Player::sendPopup(const std::string& msg) {
    auto pk = std::make_shared<network::protocol::TextPacket>();
    pk->type = network::protocol::TextPacket::TYPE_POPUP;
    pk->message = msg;
    sendPacket(pk);
}
void Player::sendTip(const std::string& msg) {
    auto pk = std::make_shared<network::protocol::TextPacket>();
    pk->type = network::protocol::TextPacket::TYPE_TIP;
    pk->message = msg;
    sendPacket(pk);
}
void Player::sendTranslation(const std::string& key) {
    sendMessage(key);
}

void Player::sendSettings() {
    auto as = std::make_shared<network::protocol::AdventureSettingsPacket>();
    uint32_t flags = 0;
    flags |= 0x02;
    flags |= 0x04;
    flags |= 0x08;
    flags |= 0x40;
    if ((cfg_.gamemode & 1) != 0)
        flags |= 0x80;
    as->flags = flags;
    as->userPermission = 2;
    as->globalPermission = 2;
    sendPacket(as);
}
bool Player::batchDataPacket(const std::vector<uint8_t>& data) {
    (void)data;
    return true;
}
} // namespace cursestone