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
#include "network/protocol/EntityEventPacket.h"
#include "utils/MainLogger.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <utility>
#include <vector>

namespace cursestone {

// Handle crafting events for protocol 84.
void Player::handleCraftingEvent(const std::array<uint8_t, 16>& recipe_uuid,
                                 const std::vector<cursestone::item::ItemStack>& input,
                                 const std::vector<cursestone::item::ItemStack>& output,
                                 uint8_t window_id) {
    if (!spawned_.load(std::memory_order_relaxed))
        return;
    if (!inv_ || !srv_)
        return;
    (void)input;
    (void)output; // Inventory contents are authoritative.

    auto* mgr = srv_->craftingManager();
    const inventory::Recipe* recipe = mgr ? mgr->getRecipeByUuidBytes(recipe_uuid.data()) : nullptr;
    if (!recipe) {
        // Resync inventory and close unknown recipe windows.
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

    // Count required ingredients by ID and metadata.
    struct Need {
        uint16_t id;
        int16_t meta;
        int count;
    };
    std::vector<Need> needs;
    auto addNeed = [&](uint16_t id, int16_t meta, int count) {
        for (auto& n : needs) {
            if (n.id == id && n.meta == meta) {
                n.count += count;
                return;
            }
        }
        needs.push_back({id, meta, count});
    };
    if (recipe->type() == inventory::RECIPE_SHAPED) {
        const auto& g = recipe->grid();
        const auto& m = recipe->gridMeta();
        for (int i = 0; i < 9; ++i) {
            if (g[i] != 0)
                addNeed(g[i], m[i], 1);
        }
    } else if (recipe->type() == inventory::RECIPE_SHAPELESS) {
        const auto& ing = recipe->ingredients();
        const auto& im = recipe->ingredientMeta();
        for (size_t i = 0; i < ing.size(); ++i) {
            addNeed(ing[i], i < im.size() ? im[i] : (int16_t)-1, 1);
        }
    } else {
        return; // Furnace recipes cannot be crafted in the grid.
    }

    // Validate ingredients against inventory slots 0-35.
    bool canCraft = true;
    std::vector<int> used(inv_->getSize(), 0);
    for (const auto& n : needs) {
        int found = 0;
        for (int slot = 0; slot < inv_->getSize(); ++slot) {
            auto it = inv_->getItem(slot);
            if (it.id != n.id)
                continue;
            if (n.meta >= 0 && it.meta != (uint16_t)n.meta)
                continue;
            int avail = it.count - used[slot];
            if (avail <= 0)
                continue;
            int take = std::min(avail, n.count - found);
            used[slot] += take;
            found += take;
            if (found >= n.count)
                break;
        }
        if (found < n.count) {
            canCraft = false;
            break;
        }
    }
    if (!canCraft) {
        utils::MainLogger::get().debug("Player " + cfg_.username +
                                       " lacks ingredients for recipe " +
                                       std::to_string(recipe->uuid()));
        sendInventory();
        return;
    }

    // Fire the cancellable crafting event.
    auto ev = event::EventManager::get().fire<event::inventory::CraftItemEvent>(
        this, std::static_pointer_cast<inventory::Inventory>(inv_), *recipe);
    if (ev.isCancelled()) {
        sendInventory();
        return;
    }

    // Consume the required ingredients.
    for (int slot = 0; slot < inv_->getSize(); ++slot) {
        if (used[slot] == 0)
            continue;
        auto it = inv_->getItem(slot);
        it.count -= used[slot];
        if (it.count <= 0)
            it = cursestone::item::ItemStack::EMPTY();
        inv_->setItem(slot, it);
    }

    // Add the result or drop any leftovers.
    auto result = recipe->result();
    if (result.count <= 0)
        result.count = 1;
    auto leftover = inv_->addItem({result});
    for (const auto& extra : leftover) {
        if (!extra.isEmpty()) {
            dropItem(pos_.x, pos_.y, pos_.z, extra.id, (uint8_t)extra.count, extra.meta, 0.0, 0.2,
                     0.0, 10, extra.nbt);
        }
    }

    // Close the crafting window and resync inventory.
    if (window_id != 0 && window_id != container_window_id_) {
        auto cp = std::make_shared<network::protocol::ContainerClosePacket>();
        cp->windowid = window_id;
        sendPacket(cp);
    }
    sendInventory();

    if (srv_ && srv_->advancedLog()) {
        utils::MainLogger::get().info("[CRAFT] " + cfg_.username +
                                      " crafted id=" + std::to_string(result.id) +
                                      " count=" + std::to_string(result.count));
    }
}

// Handle crafting when the client closes the 2x2 inventory grid.
void Player::handleInventoryCraft() {
    if (!inv_ || !srv_)
        return;
    auto* mgr = srv_->craftingManager();
    if (!mgr)
        return;

    std::array<uint16_t, 9> grid{};
    std::array<uint16_t, 9> meta{};
    bool any = false;
    for (int i = 0; i < 4; ++i) {
        auto it = inv_->getItem(i);
        if (!it.isEmpty()) {
            grid[i] = it.id;
            meta[i] = it.meta;
            any = true;
        }
    }
    if (!any)
        return;

    const inventory::Recipe* r = mgr->matchShapedWithMeta(grid, meta);
    if (!r) {
        std::vector<std::pair<uint16_t, uint16_t>> pairs; // Item ID and metadata.
        for (int i = 0; i < 4; ++i) {
            if (grid[i] != 0)
                pairs.push_back({grid[i], meta[i]});
        }
        std::sort(pairs.begin(), pairs.end());
        std::vector<uint16_t> ids, metas;
        ids.reserve(pairs.size());
        metas.reserve(pairs.size());
        for (auto& pr : pairs) {
            ids.push_back(pr.first);
            metas.push_back(pr.second);
        }
        r = mgr->matchShapelessWithMeta(ids, metas);
    }
    if (!r)
        return;

    handleCraftingEvent(r->uuidBytes(), {}, {}, 0);
}

// Spawn a dropped item entity in the world.
int64_t Player::dropItem(double x, double y, double z, uint16_t itemId, uint8_t count,
                         uint16_t damage, double motionX, double motionY, double motionZ,
                         uint32_t pickup_delay, const std::string& nbt) {
    // Ignore empty item stacks.
    if (itemId == 0 || count == 0)
        return 0;
    if (!srv_)
        return 0;
    if (motionX == 0.0 && motionY == 0.0 && motionZ == 0.0) {
        // Apply default random horizontal motion and upward velocity.
        auto lcg = []() {
            return (double)std::rand() / (double)RAND_MAX;
        };
        motionX = lcg() * 0.2 - 0.1;
        motionY = 0.2;
        motionZ = lcg() * 0.2 - 0.1;
    }

    // Allocate a runtime entity ID.
    int64_t eid = srv_->allocateEid();

    auto pk = std::make_shared<network::protocol::AddItemEntityPacket>();
    pk->eid = eid;
    pk->itemId = static_cast<int16_t>(itemId);
    pk->itemCount = count;
    pk->itemDamage = static_cast<int16_t>(damage);
    pk->itemNbt = nbt; // Preserve custom item names.
    // Preserve the exact entity position.
    pk->x = (float)x;
    pk->y = (float)y;
    pk->z = (float)z;
    pk->speedX = (float)motionX;
    pk->speedY = (float)motionY;
    pk->speedZ = (float)motionZ;

    // Send the drop to players tracking its chunk.
    int32_t pk_cx = (int)std::floor(x) >> 4;
    int32_t pk_cz = (int)std::floor(z) >> 4;
    for (auto& other : srv_->playersSnapshot()) {
        if (!other)
            continue;
        if (other->getWorld() != getWorld())
            continue; // Only send within the same world.
        if (other.get() == this) {
            // Always show the drop to its owner.
        } else if (!other->canSeeBlock(pk_cx, pk_cz)) {
            continue;
        }
        other->sendPacket(pk);
    }

    // Register the dropped item for pickup and movement updates.
    if (getWorld()) {
        world::DroppedItem di;
        di.eid = eid;
        di.itemId = itemId;
        di.count = count;
        di.damage = damage;
        di.nbt = nbt;
        di.x = x;
        di.y = y;
        di.z = z;
        di.spawn_tick = srv_->getCurrentTick();
        di.pickup_delay_ticks = pickup_delay;
        di.taken = false;
        di.vx = motionX;
        di.vy = motionY;
        di.vz = motionZ;
        // Initialize previous position at the spawn point.
        di.lx = x;
        di.ly = y;
        di.lz = z;
        getWorld()->addDroppedItem(di);
    }

    if (srv_->advancedLog()) {
        utils::MainLogger::get().info(
            "[DROP] eid=" + std::to_string(eid) + " item=" + std::to_string(itemId) + " x" +
            std::to_string(count) + " dmg=" + std::to_string(damage) + " at " + std::to_string(x) +
            "," + std::to_string(y) + "," + std::to_string(z) +
            " motion=" + std::to_string(motionX) + "," + std::to_string(motionY) + "," +
            std::to_string(motionZ) + " delay=" + std::to_string(pickup_delay));
    }
    return eid;
}

// Manage item stacks temporarily held on the cursor.
void Player::floatingAdd(const item::ItemStack& it) {
    if (it.isEmpty())
        return;
    for (auto& s : floating_items_) {
        if (s.sameItem(it)) {
            s.count += it.count;
            return;
        }
    }
    floating_items_.push_back(it);
}

item::ItemStack Player::floatingTake(const item::ItemStack& want) {
    for (auto it = floating_items_.begin(); it != floating_items_.end(); ++it) {
        if (it->id == want.id && it->meta == want.meta &&
            (want.nbt.empty() || it->nbt == want.nbt) && it->count >= want.count) {
            item::ItemStack taken = *it;
            taken.count = want.count;
            it->count -= want.count;
            if (it->count <= 0)
                floating_items_.erase(it);
            return taken;
        }
    }
    return item::ItemStack::EMPTY();
}

bool Player::floatingContains(const item::ItemStack& want) const {
    for (const auto& s : floating_items_) {
        if (s.id == want.id && s.meta == want.meta &&
            (want.nbt.empty() || s.nbt == want.nbt) && s.count >= want.count) {
            return true;
        }
    }
    return false;
}

bool Player::floatingRemove(const item::ItemStack& want) {
    return !floatingTake(want).isEmpty();
}

bool Player::beginSurvivalSlotWrite(inventory::Inventory* inv, int absSlot,
                                    const item::ItemStack& target) {
    if (!inv)
        return false;
    auto old = inv->getItem(absSlot);

    // Accept unchanged slot values.
    if (old.sameItem(target) && old.count == target.count)
        return true;

    // Calculate the item count changes caused by this write.
    item::ItemStack out = item::ItemStack::EMPTY();
    item::ItemStack in = item::ItemStack::EMPTY();
    if (old.sameItem(target)) {
        int diff = target.count - old.count;
        if (diff < 0) {
            out = old;
            out.count = -diff;
        } else {
            in = target;
            in.count = diff;
        }
    } else if (!old.isEmpty() && target.isEmpty()) {
        out = old;
    } else if (old.isEmpty() && !target.isEmpty()) {
        in = target;
    } else {
        in = target;
        out = old;
    }

    // Require added items to exist in the cursor buffer.
    if (!in.isEmpty() && !floatingRemove(in))
        return false;
    if (!out.isEmpty())
        floatingAdd(out);
    return true;
}

void Player::returnFloatingItems() {
    if (floating_items_.empty() || !inv_)
        return;
    auto leftovers = inv_->addItem(floating_items_);
    floating_items_.clear();
    for (const auto& extra : leftovers) {
        if (!extra.isEmpty()) {
            dropItem(pos_.x, pos_.y, pos_.z, extra.id, (uint8_t)extra.count, extra.meta, 0.0, 0.2,
                     0.0, 10, extra.nbt);
        }
    }
    sendInventory();
}

// Handle item drops from protocol 84 packets.
void Player::handleDropItem(network::protocol::DropItemPacket& pk) {
    if (!spawned() || !inv_)
        return;
    if (isDead())
        return; // Dead players cannot drop items.

    item::ItemStack want{};
    want.id = (uint16_t)pk.itemId;
    want.meta = (uint8_t)pk.itemDamage;
    want.count = (int)pk.itemCount;
    if (want.isEmpty())
        return;

    // Find the item in the cursor buffer, inventory, or creative packet.
    item::ItemStack dropped;
    int slot = -1;
    bool from_inventory = false;
    if (!isCreative() && floatingContains(want)) {
        dropped = floatingTake(want);
    } else if (inv_->contains(want)) {
        slot = inv_->first(want);
        if (slot < 0) {
            sendInventory();
            return;
        }
        dropped = inv_->getItem(slot);
        from_inventory = true;
    } else if (!isCreative()) {
        sendInventory();
        return;
    } else {
        dropped = want;
        if (dropped.count > 64)
            dropped.count = 64;
    }

    if (dropped.isEmpty() || dropped.count <= 0) {
        sendInventory();
        return;
    }
    auto ev = event::EventManager::get().fire<event::player::PlayerDropItemEvent>(this, dropped);
    if (ev.isCancelled()) {
        // Restore cursor items and resync the inventory.
        if (!from_inventory)
            floatingAdd(dropped);
        sendInventory();
        return;
    }

    if (from_inventory) {
        // Respect the requested drop type and available stack count.
        int drop_count = (pk.type != 0) ? dropped.count : want.count;
        if (drop_count > dropped.count)
            drop_count = dropped.count;
        if (drop_count <= 0) {
            sendInventory();
            return;
        }

        // Remove only the dropped amount from its source slot.
        item::ItemStack left = dropped;
        left.count -= drop_count;
        if (left.count <= 0) {
            inv_->clear(slot);
        } else {
            inv_->setItem(slot, left);
        }
        dropped.count = drop_count;
    }

    // Throw the item forward with a short pickup delay.
    const double kMUL = 0.15;
    math::Vector3 dir = getDirectionVector();
    dropItem(pos_.x, pos_.y + 1.3, pos_.z, dropped.id, (uint8_t)dropped.count, dropped.meta,
             dir.x * kMUL, dir.y * kMUL, dir.z * kMUL,
             40 /* Two-second pickup delay. */,
             dropped.nbt /* Preserve custom item names. */);
}

// Finish consuming the held food item.
void Player::consumeHeldItem() {
    if (!spawned() || !inv_)
        return;
    item::ItemStack held = inv_->getHeldItem();
    if (held.isEmpty())
        return;

    item::Item* def = item::Item::get(held.id);
    if (!def || !def->isFood())
        return;

    uint64_t now_ms = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::system_clock::now().time_since_epoch())
                          .count();
    // Limit consumption to once per eating animation.
    if (last_eat_ms_ != 0 && now_ms - last_eat_ms_ < 1600)
        return;
    // Require hunger unless the food is always edible.
    if (foodLevel() >= 20 && !def->alwaysEdible()) {
        sendInventory();
        return;
    }

    auto ev = event::EventManager::get().fire<event::player::PlayerItemConsumeEvent>(this, held);
    if (ev.isCancelled()) {
        sendInventory();
        return;
    }
    last_eat_ms_ = now_ms;

    // Broadcast the eating completion animation.
    {
        auto self_pk = std::make_shared<network::protocol::EntityEventPacket>();
        self_pk->eid = 0;
        self_pk->event = network::protocol::EntityEventPacket::USE_ITEM;
        sendPacket(self_pk);
        if (srv_) {
            auto other_pk = std::make_shared<network::protocol::EntityEventPacket>();
            other_pk->eid = runtimeId();
            other_pk->event = network::protocol::EntityEventPacket::USE_ITEM;
            for (auto& other : srv_->playersSnapshot()) {
                if (!other || other.get() == this)
                    continue;
                if (other->isPendingRemoval() || !other->spawned())
                    continue;
                other->sendPacket(other_pk);
            }
        }
    }

    // Restore saturation before hunger.
    food_saturation_ = std::min(20.0f, std::max(0.0f, food_saturation_ + def->foodSaturation(held.meta)));
    int restored = def->foodPoints(held.meta);
    if (restored > 0) {
        int h = foodLevel() + restored;
        setFoodLevel(h > 20 ? 20 : h);
    }
    def->applyEatEffects(*this);

    // Consume one item and return any residue.
    uint16_t residue = def->eatResidueId();
    if (held.count > 1) {
        held.count -= 1;
        inv_->setItemInHand(held);
        if (residue != 0)
            inv_->addItem({item::ItemStack{residue, 0, 1, {}, {}}});
    } else {
        inv_->setItemInHand(residue != 0 ? item::ItemStack{residue, 0, 1, {}, {}} : item::ItemStack::EMPTY());
    }
    sendInventory();
    sendFoodLevel();
}

// Send the creative inventory contents.
void Player::sendCreativeInventory() {
    bool spectator = (cfg_.gamemode == 3);
    auto pk = std::make_shared<network::protocol::ContainerSetContentPacket>();
    pk->windowid = network::protocol::ContainerSetContentPacket::SPECIAL_CREATIVE;
    if (!spectator) {
        // Load the built-in creative item list.
        pk->slots = inventory::getCreativeItems();
    }
    sendPacket(pk);
}

// Synchronize the main inventory and armor with the client.
void Player::sendInventory() {
    if (!inv_)
        return;

    // Send the main inventory with nine padding slots.
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
        // Send hotbar links using the padded slot indices.
        for (int h = 0; h < inventory::PLAYER_INV_HOTBAR_SIZE; ++h) {
            int idx = inv_->getHotbarSlotIndex(h);
            pk->hotbar.push_back(idx <= -1 ? -1 : idx + inventory::PLAYER_INV_HOTBAR_SIZE);
        }
        sendPacket(pk);
    }

    // Send the four armor slots.
    {
        auto pk = std::make_shared<network::protocol::ContainerSetContentPacket>();
        pk->windowid = network::protocol::ContainerSetContentPacket::SPECIAL_ARMOR;
        pk->slots.push_back(inv_->getHelmet());
        pk->slots.push_back(inv_->getChestplate());
        pk->slots.push_back(inv_->getLeggings());
        pk->slots.push_back(inv_->getBoots());
        sendPacket(pk);
    }

    // Send the nine hotbar slots.
    {
        auto pk = std::make_shared<network::protocol::ContainerSetContentPacket>();
        pk->windowid = network::protocol::ContainerSetContentPacket::SPECIAL_HOTBAR;
        for (int h = 0; h < 9; ++h) {
            pk->slots.push_back(inv_->getItem(h));
        }
        sendPacket(pk);
    }
}

// Send a single slot update to the client.
void Player::sendSlot(int slot) {
    if (!inv_)
        return;

    // Determine the window and slot indices.
    uint8_t windowid;
    int actual_slot = slot;

    if (slot >= inventory::PlayerInventory::ARMOR_OFFSET &&
        slot < inventory::PlayerInventory::ARMOR_OFFSET + 4) {
        // Armor slots use window ID 0x78.
        windowid = network::protocol::ContainerSetSlotPacket::SPECIAL_ARMOR;
        actual_slot = slot - inventory::PlayerInventory::ARMOR_OFFSET;
    } else if (slot >= 0 && slot < inventory::PlayerInventory::SIZE) {
        // Main inventory uses window ID 0x00.
        windowid = network::protocol::ContainerSetSlotPacket::SPECIAL_INVENTORY;
    } else {
        return; // Ignore invalid slots.
    }

    auto pk = std::make_shared<network::protocol::ContainerSetSlotPacket>();
    pk->windowid = windowid;
    pk->slot = static_cast<int16_t>(actual_slot);
    pk->hotbarSlot = static_cast<int16_t>(actual_slot);
    auto item = inv_->getItem(slot);
    pk->itemId = static_cast<int16_t>(item.id);
    pk->itemCount = item.count;
    pk->itemDamage = static_cast<int16_t>(item.meta);
    pk->itemNbt = item.nbt; // Preserve custom item names.
    sendPacket(pk);
}
} // namespace cursestone