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
#include "network/protocol/ContainerSetContentPacket.h"
#include "network/protocol/ContainerSetSlotPacket.h"
#include "network/protocol/ContainerClosePacket.h"
#include "network/protocol/AddItemEntityPacket.h"
#include "network/protocol/DropItemPacket.h"
#include "inventory/CraftingManager.h"
#include "inventory/PlayerInventory.h"
#include "inventory/CreativeItems.h"
#include "item/Item.h"
#include "event/EventManager.h"
#include "event/inventory/CraftItemEvent.h"
#include "event/player/PlayerDropItemEvent.h"
#include "event/player/PlayerItemConsumeEvent.h"
#include "utils/MainLogger.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <utility>
#include <vector>

namespace cursestone {

// ============================================================
// handleCraftingEvent — the flujo actual of crafting (protocolo 84).
//
// Portado of the case CRAFTING_EVENT_PACKET of Genisys player.php (the
// same version of protocolo that habla this server):
//   1. look for the receta by the 16 bytes of the UUID (the same that
//      escribimos in CraftingDataPacket).
//   2. check that the player actually has the ingredients in
//      its inventory (confianza zero in the packet).
//   3. CraftItemEvent (cancelable).
//   4. Consumir ingredients and dar the result.
//   5. close the window of crafting and re-sincronizar the inventory
//      with ContainerSetContentPacket windowid=0 — this way is as the
//      client 0.15.x "ve" the result.
// ============================================================
void Player::handleCraftingEvent(
        const std::array<uint8_t, 16>& recipe_uuid,
        const std::vector<cursestone::item::ItemStack>& input,
        const std::vector<cursestone::item::ItemStack>& output,
        uint8_t window_id) {
    if (!spawned_.load(std::memory_order_relaxed)) return;
    if (!inv_ || !srv_) return;
    (void)input; (void)output;  // the truth is in inv_, not in the packet

    auto* mgr = srv_->craftingManager();
    const inventory::Recipe* recipe =
        mgr ? mgr->getRecipeByUuidBytes(recipe_uuid.data()) : nullptr;
    if (!recipe) {
        // UUID unknown. Resync of the inventory and close the window
        // that the client released abierta (Genisys: sendContents + break).
        utils::MainLogger::get().debug("Player " + cfg_.username +
            " sent unknown crafting recipe UUID");
        if (window_id != 0 && window_id != container_window_id_) {
            auto cp = std::make_shared<network::protocol::ContainerClosePacket>();
            cp->windowid = window_id;
            sendPacket(cp);
        }
        sendInventory();
        return;
    }

    // ---- Conteo of ingredients requeridos (id + meta) ----
    struct Need { uint16_t id; int16_t meta; int count; };
    std::vector<Need> needs;
    auto addNeed = [&](uint16_t id, int16_t meta, int count) {
        for (auto& n : needs) {
            if (n.id == id && n.meta == meta) { n.count += count; return; }
        }
        needs.push_back({id, meta, count});
    };
    if (recipe->type() == inventory::RECIPE_SHAPED) {
        const auto& g = recipe->grid();
        const auto& m = recipe->gridMeta();
        for (int i = 0; i < 9; ++i) {
            if (g[i] != 0) addNeed(g[i], m[i], 1);
        }
    } else if (recipe->type() == inventory::RECIPE_SHAPELESS) {
        const auto& ing = recipe->ingredients();
        const auto& im = recipe->ingredientMeta();
        for (size_t i = 0; i < ing.size(); ++i) {
            addNeed(ing[i], i < im.size() ? im[i] : (int16_t)-1, 1);
        }
    } else {
        return;  // the recetas of furnace not is craftean in the grilla
    }

    // ---- Validar contra the inventory actual (slots 0..35) ----
    // meta == -1 in the receta = accepts any damage (equals with
    // anyDamage of PMMP).
    bool canCraft = true;
    std::vector<int> used(inv_->getSize(), 0);
    for (const auto& n : needs) {
        int found = 0;
        for (int slot = 0; slot < inv_->getSize(); ++slot) {
            auto it = inv_->getItem(slot);
            if (it.id != n.id) continue;
            if (n.meta >= 0 && it.meta != (uint16_t)n.meta) continue;
            int avail = it.count - used[slot];
            if (avail <= 0) continue;
            int take = std::min(avail, n.count - found);
            used[slot] += take;
            found += take;
            if (found >= n.count) break;
        }
        if (found < n.count) { canCraft = false; break; }
    }
    if (!canCraft) {
        utils::MainLogger::get().debug("Player " + cfg_.username +
            " lacks ingredients for recipe " + std::to_string(recipe->uuid()));
        sendInventory();
        return;
    }

    // ---- CraftItemEvent (cancelable) ----
    auto ev = event::EventManager::get().fire<event::inventory::CraftItemEvent>(
        this, std::static_pointer_cast<inventory::Inventory>(inv_), *recipe);
    if (ev.isCancelled()) {
        sendInventory();
        return;
    }

    // ---- Consumir ingredients (marcados in used[]) ----
    for (int slot = 0; slot < inv_->getSize(); ++slot) {
        if (used[slot] == 0) continue;
        auto it = inv_->getItem(slot);
        it.count -= used[slot];
        if (it.count <= 0) it = cursestone::item::ItemStack::EMPTY();
        inv_->setItem(slot, it);
    }

    // ---- Dar the result (or dropearlo if not cabe) ----
    auto result = recipe->result();
    if (result.count <= 0) result.count = 1;
    auto leftover = inv_->addItem({result});
    for (const auto& extra : leftover) {
        if (!extra.isEmpty()) {
            dropItem(pos_.x, pos_.y, pos_.z,
                     extra.id, (uint8_t)extra.count, extra.meta,
                     0.0, 0.2, 0.0, 10, extra.nbt);
        }
    }

    // ---- close the window of crafting + resync (the ack of the 0.15.x) ----
    if (window_id != 0 && window_id != container_window_id_) {
        auto cp = std::make_shared<network::protocol::ContainerClosePacket>();
        cp->windowid = window_id;
        sendPacket(cp);
    }
    sendInventory();

    if (srv_ && srv_->advancedLog()) {
        utils::MainLogger::get().info("[CRAFT] " + cfg_.username + " crafted id=" +
            std::to_string(result.id) + " count=" + std::to_string(result.count));
    }
}

// handleInventoryCraft — fallback "without event": when the client arma
// the receta in the inventory 2x2 and the window is closes without that haya
// llegado a CraftingEventPacket, the result remains as item ghost
// in the grilla of the client. in that close intentamos matchear the grilla
// 2x2 contra the recetas and, if matchea, corremos the same flujo of the
// event-path (validar + consumir + dar). same order of slots that
// sendInventory(): 0..3 = grilla 2x2.
void Player::handleInventoryCraft() {
    if (!inv_ || !srv_) return;
    auto* mgr = srv_->craftingManager();
    if (!mgr) return;

    std::array<uint16_t, 9> grid{};
    std::array<uint16_t, 9> meta{};
    bool any = false;
    for (int i = 0; i < 4; ++i) {
        auto it = inv_->getItem(i);
        if (!it.isEmpty()) { grid[i] = it.id; meta[i] = it.meta; any = true; }
    }
    if (!any) return;

    const inventory::Recipe* r = mgr->matchShapedWithMeta(grid, meta);
    if (!r) {
        std::vector<std::pair<uint16_t, uint16_t>> pairs;  // (id, meta)
        for (int i = 0; i < 4; ++i) {
            if (grid[i] != 0) pairs.push_back({grid[i], meta[i]});
        }
        std::sort(pairs.begin(), pairs.end());
        std::vector<uint16_t> ids, metas;
        ids.reserve(pairs.size());
        metas.reserve(pairs.size());
        for (auto& pr : pairs) { ids.push_back(pr.first); metas.push_back(pr.second); }
        r = mgr->matchShapelessWithMeta(ids, metas);
    }
    if (!r) return;

    handleCraftingEvent(r->uuidBytes(), {}, {}, 0);
}


// ============================================================
// Drop an item entity at to position — pocketmine\level\Level::dropItem()
// (Genisys 0.15), via item::spawnTo():
//
//     $pk = new AddItemEntityPacket();
//     $pk->eid = $this->getId();  $pk->x/and/z = <exact entity position>
//     $pk->speedX/and/Z = $this->motionX/and/Z;
//     $pk->item = $this->getItem();
//     $player->dataPacket($pk);
//
// The default motion is the `(lcg_value()*0.2 - 0.1, 0.2,
// lcg_value()*0.2 - 0.1)` used for block/death drops; to Q drop passes
// getDirectionVector()->multiply(0.4) instead (see handleDropItem).
// ============================================================
int64_t Player::dropItem(double x, double y, double z, uint16_t itemId,
                         uint8_t count, uint16_t damage,
                         double motionX, double motionY, double motionZ,
                         uint32_t pickup_delay, const std::string& nbt) {
    // World::dropItem() bails on an empty stack.
    if (itemId == 0 || count == 0) return 0;
    if (!srv_) return 0;
    if (motionX == 0.0 && motionY == 0.0 && motionZ == 0.0) {
        // World::dropItem()'s default: `$motion = new Vector3(
        // lcg_value() * 0.2 - 0.1, 0.2, lcg_value() * 0.2 - 0.1)`. The
        // upward nudge is what makes to broken block's drop pop out of the
        // block it came from instead of sinking into it.
        auto lcg = []() {
            return (double)std::rand() / (double)RAND_MAX;
        };
        motionX = lcg() * 0.2 - 0.1;
        motionY = 0.2;
        motionZ = lcg() * 0.2 - 0.1;
    }

    // Allocate to runtime entity id (PMMP `Level::$entityIdCounter`).
    int64_t eid = srv_->allocateEid();

    auto pk = std::make_shared<network::protocol::AddItemEntityPacket>();
    pk->eid        = eid;
    pk->itemId     = static_cast<int16_t>(itemId);
    pk->itemCount  = count;
    pk->itemDamage = static_cast<int16_t>(damage);
    pk->itemNbt    = nbt;   // custom-named drops keep their name
    // Genisys uses the raw entity position — NOT floor(x)+0.5. Rounding to
    // the block centre is what used to make to dropped stack appear half to
    // block off from where the client thought it was.
    pk->x = (float)x;
    pk->y = (float)y;
    pk->z = (float)z;
    pk->speedX = (float)motionX;
    pk->speedY = (float)motionY;
    pk->speedZ = (float)motionZ;

    // Broadcast only to players tracking this chunk (PMMP `addChunkPacket`).
    int32_t pk_cx = (int)std::floor(x) >> 4;
    int32_t pk_cz = (int)std::floor(z) >> 4;
    for (auto& other : srv_->playersSnapshot()) {
        if (!other) continue;
        if (other->getWorld() != getWorld()) continue;   // same world
        if (other.get() == this) {
            // Always send to the dropper too so they see their own drop.
        } else if (!other->canSeeBlock(pk_cx, pk_cz)) {
            continue;
        }
        other->sendPacket(pk);
    }

    // Register the dropped item in the World's registry so it can be
    // picked up, and so World::tickDroppedItems() can integrate the very
    // same motion the client is integrating.
    if (getWorld()) {
        world::DroppedItem di;
        di.eid        = eid;
        di.itemId     = itemId;
        di.count      = count;
        di.damage     = damage;
        di.nbt        = nbt;
        di.x          = x;
        di.y          = y;
        di.z          = z;
        di.spawn_tick = srv_->getCurrentTick();
        di.pickup_delay_ticks = pickup_delay;
        di.taken     = false;
        di.vx        = motionX;
        di.vy        = motionY;
        di.vz        = motionZ;
        // $lastX/$lastY/$lastZ — start at the spawn point so the very
        // first updateMovement() only fires once the item has really moved.
        di.lx = x; di.ly = y; di.lz = z;
        getWorld()->addDroppedItem(di);
    }

    if (srv_->advancedLog()) {
        utils::MainLogger::get().info("[DROP] eid=" + std::to_string(eid) +
            " item=" + std::to_string(itemId) + " x" + std::to_string(count) +
            " dmg=" + std::to_string(damage) + " at " +
            std::to_string(x) + "," + std::to_string(y) + "," + std::to_string(z) +
            " motion=" + std::to_string(motionX) + "," +
            std::to_string(motionY) + "," + std::to_string(motionZ) +
            " delay=" + std::to_string(pickup_delay));
    }
    return eid;
}

// ============================================================
// handleDropItem — portado 1:1 of Genisys player::handleDataPacket
// case DROP_ITEM_PACKET (protocolo 84):
//
//   if($this->spawned === false or $this->blocked or !$this->isAlive()) break;
//   if(!$this->inventory->contains($packet->item) ...) { sendContents; break; }
//   $slot = $this->inventory->first($packet->item);
//   if($slot == -1) { sendContents; break; }
//   $dropItem = $this->inventory->getItem($slot);
//   $ev = new PlayerDropItemEvent($this, $dropItem);
//   if($ev->isCancelled()) { sendSlot($slot); break; }
//   $this->inventory->remove($dropItem);
//   $motion = $this->getDirectionVector()->multiply(0.4);
//   $this->level->dropItem($this->add(0, 1.3, 0), $dropItem, $motion, 40);
//
// Two deviations on purpose:
//   * Genisys' BaseInventory::remove() wipes EVERY slot holding the same
//     id+meta, so to hotbar with two stacks of cobble loses both while only
//     one entity is spawned. We remove exactly `count` from the slot
//     `first()` found, which is what `removeItem()` is for.
//   * Genisys ignores DropItemPacket::$type and always throws the whole
//     stack. $type is the Q / Ctrl+Q distinction (0 = one, 1 = the stack),
//     so honouring it is what keeps the client's optimistic animation and
//     the server's inventory in agreement.
// ============================================================
void Player::handleDropItem(network::protocol::DropItemPacket& pk) {
    if (!spawned() || !inv_) return;
    if (isDead()) return;   // Genisys: `if(... or !$this->isAlive()) break;`

    item::ItemStack want{};
    want.id    = (uint16_t)pk.itemId;
    want.meta  = (uint8_t)pk.itemDamage;
    want.count = (int)pk.itemCount;
    if (want.isEmpty()) return;

    if (!inv_->contains(want)) { sendInventory(); return; }
    int slot = inv_->first(want);
    if (slot < 0) { sendInventory(); return; }

    item::ItemStack dropped = inv_->getItem(slot);
    if (dropped.isEmpty() || dropped.count <= 0) { sendInventory(); return; }
    auto ev = event::EventManager::get().fire<event::player::PlayerDropItemEvent>(this, dropped);
    if (ev.isCancelled()) { sendSlot(slot); return; }

    // type 1 = "drop the stack" (Ctrl+Q); type 0 = "drop what the packet
    // asked for". Clamp to what the slot actually holds so to spoofed
    // count can never clone or void items.
    int drop_count = (pk.type != 0) ? dropped.count : want.count;
    if (drop_count > dropped.count) drop_count = dropped.count;
    if (drop_count <= 0) { sendInventory(); return; }

    // Take it out of the slot it came from — decrement and only clear the
    // slot when it runs out. BaseInventory::remove() used to erase the map
    // entry while iterating it (SIGSEGV on every Q press); this is the
    // safe single-indexed form. clear()/setItem() each fire onSlotChange,
    // which sends the ContainerSetSlot ack the client needs.
    item::ItemStack left = dropped;
    left.count -= drop_count;
    if (left.count <= 0) {
        inv_->clear(slot);
    } else {
        inv_->setItem(slot, left);
    }

    // $this->level->dropItem($this->add(0, 1.3, 0), $dropItem, $motion, 40)
    //
    // The motion magnitude is 0.15, not Genisys' 0.4. With 0.4 the item
    // is launched at 0.4 blocks/tick with drag 0.02 and, once it lands,
    // keeps sliding at the block friction factor — measured with this
    // exact integrator it comes to rest 3.48 blocks from the player,
    // against to pickup reach of 0.925. So to Q-dropped item ALWAYS landed
    // outside the box the server tests, which is the whole of "to veces
    // puedo and to veces not": you drop it, walk to where you dropped it, and
    // there is nothing there. At 0.15 it comes to rest ~1.3 blocks out and
    // the ground magnet in World::tickDroppedItems() closes the rest, so
    // the item is always where you dropped it and always collectable.
    const double kMUL = 0.15;
    math::Vector3 dir = getDirectionVector();
    dropItem(pos_.x, pos_.y + 1.3, pos_.z,
             dropped.id, (uint8_t)drop_count, dropped.meta,
             dir.x * kMUL, dir.y * kMUL, dir.z * kMUL,
             40 /* PickupDelay: 2 s, Genisys' manual-drop value */,
             dropped.nbt /* a named stack keeps its name when thrown */);
}

// ============================================================
// consumeHeldItem — portado of Genisys player::consumeHeldItem().
// Llamado from ENTITY_EVENT_PACKET (evento 9, Eating). only the items
// FoodSource have efecto; the rest is not-op.
// ============================================================
void Player::consumeHeldItem() {
    if (!spawned() || !inv_) return;
    item::ItemStack held = inv_->getHeldItem();
    if (held.isEmpty()) return;

    item::Item* def = item::Item::get(held.id);
    if (!def || !def->isFood()) return;

    auto ev = event::EventManager::get().fire<event::player::PlayerItemConsumeEvent>(this, held);
    if (ev.isCancelled()) { sendInventory(); return; }

    int restored = def->foodPoints();
    if (restored > 0) {
        int h = foodLevel() + restored;
        setFoodLevel(h > 20 ? 20 : h);
    }
    held.count -= 1;
    inv_->setItemInHand(held.count > 0 ? held : item::ItemStack::EMPTY());
    sendInventory();
}

// ============================================================
// Send the creative-inventory contents (PMMP player.php:2301 + 1461)
// ============================================================
void Player::sendCreativeInventory() {
    bool spectator = (cfg_.gamemode == 3);
    auto pk = std::make_shared<network::protocol::ContainerSetContentPacket>();
    pk->windowid = network::protocol::ContainerSetContentPacket::SPECIAL_CREATIVE;
    if (!spectator) {
        // Use the hardcoded creative-items list (not JSON dependency —
        // mirrors Nukkit's approach of compiling the list at build time).
        pk->slots = inventory::getCreativeItems();
    }
    sendPacket(pk);
}

// ============================================================
// sendInventory — push the player's main inventory + armor to the client.
// Called on doFirstSpawn() and after any inventory change so the client
// UI stays in sync with the server's authoritative state. Mirrors
// Nukkit player.sendInventory() + player.sendArmor().
// ============================================================
void Player::sendInventory() {
    if (!inv_) return;

    // main inventory (windowid 0x00): 45 slots (36 main + 9 hotbar).
    //
    // bug FIX: this only ever pushed the 36 actual slots — the comment
    // above already said "45 slots" but the loop below it never matched
    // that. actual PMMP's PlayerInventory::sendContents() (protocol 84 /
    // MCPE 0.15.10) pads 9 extra dummy AIR slots on top of the 36 actual
    // ones, with the comment "Because PE is stupid and shows 9 less
    // slots than you send it, give it 9 dummy slots so it shows all the
    // actual slots." Without that padding, the MCPE 0.15.x client's
    // inventory UI under-renders this container by exactly 9 slots, so
    // some of the actual, server-side items in the tail of the 36-slot
    // array never show up on screen even though inv_ has them — the
    // same "item exists server-side but isn't visible client-side"
    // symptom as the creative-drop bug.
    {
        auto pk = std::make_shared<network::protocol::ContainerSetContentPacket>();
        pk->windowid = network::protocol::ContainerSetContentPacket::SPECIAL_INVENTORY;
        pk->slots.reserve(inventory::PlayerInventory::SIZE + 9);
        for (int i = 0; i < inventory::PlayerInventory::SIZE; ++i) {
            pk->slots.push_back(inv_->getItem(i));
        }
        for (int i = 0; i < 9; ++i) {
            pk->slots.push_back(item::ItemStack::EMPTY());
        }
        // Hotbar link indices.
        //
        // bug actual ENCONTRADO (comparando contra PlayerInventory::sendContents()
        // actual of PMMP protocolo 84 / MCPE 0.15.10):
        //
        //   foreach ($target as $player) {
        //       for ($i = 0; $i < $this->getHotbarSize(); ++$i) {
        //           $index = $this->getHotbarSlotIndex($i);
        //           $pk->hotbar[$i] = $index <= -1 ? -1 : $index + $this->getHotbarSize();
        //       }
        //   }
        //
        // the valor actual that va in `hotbar[i]` not is the index crudo of the slot
        // (0..8) — is that index + 9 (getHotbarSize()). Esto is because
        // `slots` in the packet already trae, first, the 36 slots reales and
        // LUEGO 9 slots dummy of AIR pegados to the end (the padding "PE is
        // stupid and shows 9 less slots" that already aplicamos above) — and the
        // array `hotbar` not apunta to the inventory lógico, apunta to the
        // position inside of that array `slots` already paddeado. therefore the +9:
        // hotbar[i] has that señalar the position actual inside of `slots`
        // where vive that item, and the primeras 9 positions of that
        // numbering "with offset" siguen being, confusingly, for the UI
        // of the hotbar in screen — but the index that is takes precedence is the of the
        // sección of 36, not the of 0..8 directo.
        //
        // this function sent `pk->hotbar.push_back(h)` with h=0..8 without
        // sumar 9 and without consultar the link actual (inv_->getHotbarSlotIndex(h)),
        // assuming always the link identity. with that, the client receives
        // a array hotbar that apunta to the positions 0..8 of `slots` in
        // time of 9..17 — exactly where yes are the primeros 9 slots
        // reales of the inventory (recordar: slots[0..35]=reales,
        // slots[36..44]=dummy AIR). the result: the client arma its UI of
        // hotbar apuntando to the sección equivocada of the array, and only by
        // match numeric a slot can arrive to verse well — the
        // rest remains out of sync and not reflects the changes by
        // ContainerSetSlotPacket subsequent (therefore "only podía take out of the
        // slot that already had something": that slot is the only cuyo state actual
        // the client already knew by other vía).
        //
        // Fix: use the link actual (inv_->getHotbarSlotIndex(h), normalmente
        // identity salvo that something it haya cambiado) and sumarle
        // PLAYER_INV_HOTBAR_SIZE (9), just like the PMMP actual.
        for (int h = 0; h < inventory::PLAYER_INV_HOTBAR_SIZE; ++h) {
            int idx = inv_->getHotbarSlotIndex(h);
            pk->hotbar.push_back(idx <= -1 ? -1 : idx + inventory::PLAYER_INV_HOTBAR_SIZE);
        }
        sendPacket(pk);
    }

    // Armor (windowid 0x78): 4 slots.
    {
        auto pk = std::make_shared<network::protocol::ContainerSetContentPacket>();
        pk->windowid = network::protocol::ContainerSetContentPacket::SPECIAL_ARMOR;
        pk->slots.push_back(inv_->getHelmet());
        pk->slots.push_back(inv_->getChestplate());
        pk->slots.push_back(inv_->getLeggings());
        pk->slots.push_back(inv_->getBoots());
        sendPacket(pk);
    }

    // Hotbar (windowid 0x7a): 9 slots mirroring the hotbar portion of
    // the main inventory (slots 0..8). Nukkit sends this too so the
    // client immediately highlights the correct hotbar slot.
    {
        auto pk = std::make_shared<network::protocol::ContainerSetContentPacket>();
        pk->windowid = network::protocol::ContainerSetContentPacket::SPECIAL_HOTBAR;
        for (int h = 0; h < 9; ++h) {
            pk->slots.push_back(inv_->getItem(h));
        }
        sendPacket(pk);
    }
}

// ============================================================
// sendSlot — send to ContainerSetSlotPacket for to single slot back to
// the client. Mirrors Nukkit's PlayerInventory.sendSlot(index, player):
// after the server processes to ContainerSetSlotPacket from the client
// (e.g. creative inventory click), it MUST send to confirmation packet
// back. Without this, MCPE 0.15.x reverts the slot change visually —
// the item disappears from the client UI even though it's in the
// server's inventory.
// ============================================================
void Player::sendSlot(int slot) {
    if (!inv_) return;

    // Determine the windowid and the current slot index.
    uint8_t windowid;
    int actual_slot = slot;

    if (slot >= inventory::PlayerInventory::ARMOR_OFFSET &&
        slot <  inventory::PlayerInventory::ARMOR_OFFSET + 4) {
        // Armor slot — windowid 0x78, slot 0..3.
        windowid = network::protocol::ContainerSetSlotPacket::SPECIAL_ARMOR;
        actual_slot = slot - inventory::PlayerInventory::ARMOR_OFFSET;
    } else if (slot >= 0 && slot < inventory::PlayerInventory::SIZE) {
        // main inventory — windowid 0x00.
        windowid = network::protocol::ContainerSetSlotPacket::SPECIAL_INVENTORY;
    } else {
        return;  // Invalid slot.
    }

    auto pk = std::make_shared<network::protocol::ContainerSetSlotPacket>();
    pk->windowid   = windowid;
    pk->slot       = static_cast<int16_t>(actual_slot);
    pk->hotbarSlot = static_cast<int16_t>(actual_slot);
    auto item = inv_->getItem(slot);
    pk->itemId     = static_cast<int16_t>(item.id);
    pk->itemCount  = item.count;
    pk->itemDamage = static_cast<int16_t>(item.meta);
    pk->itemNbt    = item.nbt;   // custom-named items keep their name
    sendPacket(pk);
}} // namespace cursestone
