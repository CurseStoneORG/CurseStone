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
// handleCraftingEvent — el flujo real de crafteo (protocolo 84).
//
// Portado del case CRAFTING_EVENT_PACKET de Genisys Player.php (la
// MISMA versión de protocolo que habla este server):
//   1. Buscar la receta por los 16 bytes del UUID (los mismos que
//      escribimos en CraftingDataPacket).
//   2. Verificar que el jugador REALMENTE tiene los ingredientes en
//      su inventario (confianza cero en el packet).
//   3. CraftItemEvent (cancelable).
//   4. Consumir ingredientes y dar el resultado.
//   5. Cerrar la ventana de crafteo y re-sincronizar el inventario
//      con ContainerSetContentPacket windowid=0 — así es como el
//      cliente 0.15.x "ve" el resultado.
// ============================================================
void Player::handleCraftingEvent(
        const std::array<uint8_t, 16>& recipe_uuid,
        const std::vector<cursestone::item::ItemStack>& input,
        const std::vector<cursestone::item::ItemStack>& output,
        uint8_t window_id) {
    if (!spawned_.load(std::memory_order_relaxed)) return;
    if (!inv_ || !srv_) return;
    (void)input; (void)output;  // la verdad está en inv_, no en el packet

    auto* mgr = srv_->craftingManager();
    const inventory::Recipe* recipe =
        mgr ? mgr->getRecipeByUuidBytes(recipe_uuid.data()) : nullptr;
    if (!recipe) {
        // UUID desconocido. Resync del inventario y cerrar la ventana
        // que el cliente dejó abierta (Genisys: sendContents + break).
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

    // ---- Conteo de ingredientes requeridos (id + meta) ----
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
        return;  // las recetas de horno no se craftean en la grilla
    }

    // ---- Validar contra el inventario real (slots 0..35) ----
    // meta == -1 en la receta = acepta cualquier damage (equals con
    // anyDamage de PMMP).
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

    // ---- Consumir ingredientes (marcados en used[]) ----
    for (int slot = 0; slot < inv_->getSize(); ++slot) {
        if (used[slot] == 0) continue;
        auto it = inv_->getItem(slot);
        it.count -= used[slot];
        if (it.count <= 0) it = cursestone::item::ItemStack::EMPTY();
        inv_->setItem(slot, it);
    }

    // ---- Dar el resultado (o dropearlo si no cabe) ----
    auto result = recipe->result();
    if (result.count <= 0) result.count = 1;
    auto leftover = inv_->addItem({result});
    for (const auto& extra : leftover) {
        if (!extra.isEmpty()) {
            dropItem(pos_.x, pos_.y, pos_.z,
                     extra.id, (uint8_t)extra.count, extra.meta);
        }
    }

    // ---- Cerrar la ventana de crafteo + resync (el ack del 0.15.x) ----
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

// handleInventoryCraft — fallback "sin event": cuando el cliente arma
// la receta EN el inventario 2x2 y la ventana se cierra sin que haya
// llegado un CraftingEventPacket, el resultado queda como item fantasma
// en la grilla del cliente. En ese close intentamos matchear la grilla
// 2x2 contra las recetas y, si matchea, corremos el mismo flujo del
// event-path (validar + consumir + dar). Mismo orden de slots que
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
// Drop an item entity at a position — pocketmine\level\Level::dropItem()
// (Genisys 0.15), via Item::spawnTo():
//
//     $pk = new AddItemEntityPacket();
//     $pk->eid = $this->getId();  $pk->x/y/z = <exact entity position>
//     $pk->speedX/Y/Z = $this->motionX/Y/Z;
//     $pk->item = $this->getItem();
//     $player->dataPacket($pk);
//
// The default motion is the `(lcg_value()*0.2 - 0.1, 0.2,
// lcg_value()*0.2 - 0.1)` used for block/death drops; a Q drop passes
// getDirectionVector()->multiply(0.4) instead (see handleDropItem).
// ============================================================
int64_t Player::dropItem(double x, double y, double z, uint16_t itemId,
                         uint8_t count, uint16_t damage,
                         double motionX, double motionY, double motionZ,
                         uint32_t pickup_delay) {
    // World::dropItem() bails on an empty stack.
    if (itemId == 0 || count == 0) return 0;
    if (!srv_) return 0;
    if (motionX == 0.0 && motionY == 0.0 && motionZ == 0.0) {
        // World::dropItem()'s default: `$motion = new Vector3(
        // lcg_value() * 0.2 - 0.1, 0.2, lcg_value() * 0.2 - 0.1)`. The
        // upward nudge is what makes a broken block's drop pop out of the
        // block it came from instead of sinking into it.
        auto lcg = []() {
            return (double)std::rand() / (double)RAND_MAX;
        };
        motionX = lcg() * 0.2 - 0.1;
        motionY = 0.2;
        motionZ = lcg() * 0.2 - 0.1;
    }

    // Allocate a runtime entity ID (PMMP `Level::$entityIdCounter`).
    int64_t eid = srv_->allocateEid();

    auto pk = std::make_shared<network::protocol::AddItemEntityPacket>();
    pk->eid        = eid;
    pk->itemId     = static_cast<int16_t>(itemId);
    pk->itemCount  = count;
    pk->itemDamage = static_cast<int16_t>(damage);
    // Genisys uses the raw entity position — NOT floor(x)+0.5. Rounding to
    // the block centre is what used to make a dropped stack appear half a
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
    if (srv_->getDefaultWorld()) {
        world::DroppedItem di;
        di.eid        = eid;
        di.itemId     = itemId;
        di.count      = count;
        di.damage     = damage;
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
        srv_->getDefaultWorld()->addDroppedItem(di);
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
// handleDropItem — portado 1:1 de Genisys Player::handleDataPacket
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
//     id+meta, so a hotbar with two stacks of cobble loses both while only
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
    // asked for". Clamp to what the slot actually holds so a spoofed
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
    // against a pickup reach of 0.925. So a Q-dropped item ALWAYS landed
    // outside the box the server tests, which is the whole of "a veces
    // puedo y a veces no": you drop it, walk to where you dropped it, and
    // there is nothing there. At 0.15 it comes to rest ~1.3 blocks out and
    // the ground magnet in World::tickDroppedItems() closes the rest, so
    // the item is always where you dropped it and always collectable.
    const double kMUL = 0.15;
    math::Vector3 dir = getDirectionVector();
    dropItem(pos_.x, pos_.y + 1.3, pos_.z,
             dropped.id, (uint8_t)drop_count, dropped.meta,
             dir.x * kMUL, dir.y * kMUL, dir.z * kMUL,
             40 /* PickupDelay: 2 s, Genisys' manual-drop value */);
}

// ============================================================
// consumeHeldItem — portado de Genisys Player::consumeHeldItem().
// Llamado desde ENTITY_EVENT_PACKET (evento 9, Eating). Sólo los items
// FoodSource tienen efecto; el resto es no-op.
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
// Send the creative-inventory contents (PMMP Player.php:2301 + 1461)
// ============================================================
void Player::sendCreativeInventory() {
    bool spectator = (cfg_.gamemode == 3);
    auto pk = std::make_shared<network::protocol::ContainerSetContentPacket>();
    pk->windowid = network::protocol::ContainerSetContentPacket::SPECIAL_CREATIVE;
    if (!spectator) {
        // Use the hardcoded creative-items list (no JSON dependency —
        // mirrors Nukkit's approach of compiling the list at build time).
        pk->slots = inventory::getCreativeItems();
    }
    sendPacket(pk);
}

// ============================================================
// sendInventory — push the player's main inventory + armor to the client.
// Called on doFirstSpawn() and after any inventory change so the client
// UI stays in sync with the server's authoritative state. Mirrors
// Nukkit Player.sendInventory() + Player.sendArmor().
// ============================================================
void Player::sendInventory() {
    if (!inv_) return;

    // Main inventory (windowid 0x00): 45 slots (36 main + 9 hotbar).
    //
    // BUG FIX: this only ever pushed the 36 real slots — the comment
    // above already said "45 slots" but the loop below it never matched
    // that. Real PMMP's PlayerInventory::sendContents() (protocol 84 /
    // MCPE 0.15.10) pads 9 extra dummy AIR slots on top of the 36 real
    // ones, with the comment "Because PE is stupid and shows 9 less
    // slots than you send it, give it 9 dummy slots so it shows all the
    // REAL slots." Without that padding, the MCPE 0.15.x client's
    // inventory UI under-renders this container by exactly 9 slots, so
    // some of the real, server-side items in the tail of the 36-slot
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
        // BUG REAL ENCONTRADO (comparando contra PlayerInventory::sendContents()
        // real de PMMP protocolo 84 / MCPE 0.15.10):
        //
        //   foreach ($target as $player) {
        //       for ($i = 0; $i < $this->getHotbarSize(); ++$i) {
        //           $index = $this->getHotbarSlotIndex($i);
        //           $pk->hotbar[$i] = $index <= -1 ? -1 : $index + $this->getHotbarSize();
        //       }
        //   }
        //
        // El valor real que va en `hotbar[i]` NO es el índice crudo del slot
        // (0..8) — es ESE índice + 9 (getHotbarSize()). Esto es porque
        // `slots` en el paquete ya trae, primero, los 36 slots reales y
        // LUEGO 9 slots dummy de AIR pegados al final (el padding "PE is
        // stupid and shows 9 less slots" que ya aplicamos arriba) — y el
        // array `hotbar` no apunta al inventario lógico, apunta a la
        // posición DENTRO de ese array `slots` ya paddeado. Por eso el +9:
        // hotbar[i] tiene que señalar la posición real dentro de `slots`
        // donde vive ese item, y las primeras 9 posiciones de esa
        // numeración "con offset" siguen siendo, confusamente, para la UI
        // del hotbar en pantalla — pero el índice que se manda es el de la
        // sección de 36, no el de 0..8 directo.
        //
        // Esta función mandaba `pk->hotbar.push_back(h)` con h=0..8 SIN
        // sumar 9 y SIN consultar el link real (inv_->getHotbarSlotIndex(h)),
        // asumiendo siempre el link identidad. Con eso, el cliente recibe
        // un array hotbar que apunta a las posiciones 0..8 de `slots` en
        // vez de 9..17 — exactamente donde SÍ están los primeros 9 slots
        // reales del inventario (recordar: slots[0..35]=reales,
        // slots[36..44]=dummy AIR). El resultado: el cliente arma su UI de
        // hotbar apuntando a la sección equivocada del array, y sólo por
        // COINCIDENCIA numérica un slot puede llegar a verse bien — el
        // resto queda desincronizado y no refleja los cambios por
        // ContainerSetSlotPacket posteriores (por eso "solo podía sacar del
        // slot que ya tenía algo": ese slot es el único cuyo estado real
        // el cliente ya conocía por otra vía).
        //
        // Fix: usar el link real (inv_->getHotbarSlotIndex(h), normalmente
        // identidad salvo que algo lo haya cambiado) y sumarle
        // PLAYER_INV_HOTBAR_SIZE (9), igual que el PMMP real.
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
// sendSlot — send a ContainerSetSlotPacket for a single slot back to
// the client. Mirrors Nukkit's PlayerInventory.sendSlot(index, player):
// after the server processes a ContainerSetSlotPacket from the client
// (e.g. creative inventory click), it MUST send a confirmation packet
// back. Without this, MCPE 0.15.x reverts the slot change visually —
// the item disappears from the client UI even though it's in the
// server's inventory.
// ============================================================
void Player::sendSlot(int slot) {
    if (!inv_) return;

    // Determine the windowid and the actual slot index.
    uint8_t windowid;
    int actual_slot = slot;

    if (slot >= inventory::PlayerInventory::ARMOR_OFFSET &&
        slot <  inventory::PlayerInventory::ARMOR_OFFSET + 4) {
        // Armor slot — windowid 0x78, slot 0..3.
        windowid = network::protocol::ContainerSetSlotPacket::SPECIAL_ARMOR;
        actual_slot = slot - inventory::PlayerInventory::ARMOR_OFFSET;
    } else if (slot >= 0 && slot < inventory::PlayerInventory::SIZE) {
        // Main inventory — windowid 0x00.
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
    sendPacket(pk);
}} // namespace cursestone
