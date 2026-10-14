/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

 * CurseStone | Open Source Server Software
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
// SPDX-License-Identifier: LGPL-3.0-or-later
// BaseInventory.cpp — ported from cn.nukkit.inventory.BaseInventory
#include "BaseInventory.h"
#include "InventoryHolder.h"
#include "../item/ItemStack.h"
#include "../Player.h"
#include "../network/protocol/ContainerSetContentPacket.h"
#include "../network/protocol/ContainerSetSlotPacket.h"
#include "../utils/MainLogger.hpp"

#include <algorithm>
#include <cmath>

namespace cursestone::inventory {

// ---- Constructors ----
BaseInventory::BaseInventory(InventoryHolder* holder, std::shared_ptr<InventoryType> type)
    : BaseInventory(holder, type, {}, type ? type->getDefaultSize() : 0, "") {}

BaseInventory::BaseInventory(InventoryHolder* holder, std::shared_ptr<InventoryType> type,
                               const std::unordered_map<int, item::ItemStack>& items)
    : BaseInventory(holder, type, items, type ? type->getDefaultSize() : 0, "") {}

BaseInventory::BaseInventory(InventoryHolder* holder, std::shared_ptr<InventoryType> type,
                               const std::unordered_map<int, item::ItemStack>& items,
                               int overrideSize)
    : BaseInventory(holder, type, items, overrideSize, "") {}

BaseInventory::BaseInventory(InventoryHolder* holder, std::shared_ptr<InventoryType> type,
                               const std::unordered_map<int, item::ItemStack>& items,
                               int overrideSize, const std::string& overrideTitle)
    : type_(type), holder_(holder) {
    if (overrideSize > 0) size_ = overrideSize;
    else if (type_)      size_ = type_->getDefaultSize();
    if (!overrideTitle.empty()) title_ = overrideTitle;
    else if (type_)            title_ = type_->getDefaultTitle();
    if (type_)                 name_  = type_->getDefaultTitle();
    type_raw_ = type_.get();
    setContents(items);
}

// ---- getItem / setItem ----
item::ItemStack BaseInventory::getItem(int index) {
    auto it = slots_.find(index);
    if (it == slots_.end()) return item::ItemStack::EMPTY();
    return it->second;
}

bool BaseInventory::setItem(int index, const item::ItemStack& item) {
    if (index < 0 || index >= size_) return false;
    if (item.id == 0 || item.count <= 0) return clear(index);

    // (Nukkit fires EntityInventoryChangeEvent here — we skip the event
    //  layer and just store the item.)
    item::ItemStack old = getItem(index);
    slots_[index] = item;
    onSlotChange(index, old);
    return true;
}

// ---- canAddItem ----
bool BaseInventory::canAddItem(const item::ItemStack& item) {
    if (item.isEmpty()) return true;
    int remaining = item.count;
    int max_stack = maxStackSize_;
    // getSize() (virtual) — NOT size_ directly. PlayerInventory
    // overrides getSize() to return 36 (main only, no armor) even
    // though its underlying size_ is 40 (36 main + 4 armor). Using
    // size_ here would count empty armor slots as pickup capacity,
    // and addItem() below (same fix) would then actually place a
    // picked-up item into an armor slot. See BaseInventory::addItem.
    int limit = getSize();
    for (int i = 0; i < limit && remaining > 0; ++i) {
        item::ItemStack slot = getItem(i);
        // sameItem (not bare id+meta): a custom-named stack must never
        // merge with an unnamed one, otherwise the name would disappear.
        if (slot.sameItem(item) && slot.count > 0) {
            int diff = max_stack - slot.count;
            if (diff > 0) remaining -= diff;
        } else if (slot.id == 0) {
            remaining -= max_stack;
        }
        if (remaining <= 0) return true;
    }
    return remaining <= 0;
}

// ---- addItem ----
std::vector<item::ItemStack> BaseInventory::addItem(std::vector<item::ItemStack> slots) {
    std::vector<item::ItemStack> itemSlots;
    for (auto& s : slots) {
        if (s.id != 0 && s.count > 0) itemSlots.push_back(s);
    }
    std::vector<int> emptySlots;
    int max_stack = maxStackSize_;
    // getSize() (virtual), not size_ — see canAddItem's comment above.
    // Without this, a full-main-inventory pickup could land in an
    // empty armor slot (helmet/chestplate/leggings/boots), since
    // PlayerInventory's size_ (40) includes them but getSize() (36)
    // deliberately doesn't.
    int limit = getSize();

    for (int i = 0; i < limit; ++i) {
        item::ItemStack item = getItem(i);
        if (item.id == 0 || item.count <= 0) emptySlots.push_back(i);

        for (auto it = itemSlots.begin(); it != itemSlots.end(); ) {
            auto& slot = *it;
            // sameItem: merge only id+meta+NBT-equal stacks (see canAddItem).
            if (slot.sameItem(item) &&
                item.count < max_stack) {
                int amount = std::min(max_stack - item.count, slot.count);
                amount = std::min(amount, max_stack);
                if (amount > 0) {
                    slot.count  -= amount;
                    item.count   += amount;
                    setItem(i, item);
                    if (slot.count <= 0) {
                        it = itemSlots.erase(it);
                        continue;
                    }
                }
            }
            ++it;
        }
        if (itemSlots.empty()) break;
    }

    if (!itemSlots.empty() && !emptySlots.empty()) {
        for (int slotIndex : emptySlots) {
            if (itemSlots.empty()) break;
            auto& slot = itemSlots.front();
            int amount = std::min(max_stack, slot.count);
            slot.count -= amount;
            item::ItemStack newItem = slot;
            newItem.count = amount;
            setItem(slotIndex, newItem);
            if (slot.count <= 0) itemSlots.erase(itemSlots.begin());
        }
    }
    return itemSlots;
}

// ---- removeItem ----
std::vector<item::ItemStack> BaseInventory::removeItem(std::vector<item::ItemStack> slots) {
    std::vector<item::ItemStack> itemSlots;
    for (auto& s : slots) {
        if (s.id != 0 && s.count > 0) itemSlots.push_back(s);
    }
    // getSize() (virtual), not size_ — kept consistent with addItem/
    // canAddItem above so a rollback (see Player.cpp's dropped-item
    // pickup race handling) removes from the same range addItem() was
    // scoped to, not a wider one that could reach into armor slots.
    int limit = getSize();
    for (int i = 0; i < limit; ++i) {
        item::ItemStack item = getItem(i);
        if (item.id == 0 || item.count <= 0) continue;
        for (auto it = itemSlots.begin(); it != itemSlots.end(); ) {
            auto& slot = *it;
            if (slot.id == item.id && slot.meta == item.meta) {
                int amount = std::min(item.count, slot.count);
                slot.count -= amount;
                item.count -= amount;
                setItem(i, item);
                if (slot.count <= 0) {
                    it = itemSlots.erase(it);
                    continue;
                }
            }
            ++it;
        }
        if (itemSlots.empty()) break;
    }
    return itemSlots;
}

// ---- getContents / setContents ----
std::unordered_map<int, item::ItemStack> BaseInventory::getContents() {
    return slots_;
}

void BaseInventory::setContents(const std::unordered_map<int, item::ItemStack>& items) {
    for (int i = 0; i < size_; ++i) {
        auto it = items.find(i);
        if (it == items.end()) {
            if (slots_.count(i)) clear(i);
        } else {
            if (!setItem(i, it->second)) clear(i);
        }
    }
}

// ---- contains / all / first / firstEmpty / remove ----
bool BaseInventory::contains(const item::ItemStack& item) {
    int count = std::max(1, item.count);
    for (auto& [_, slot] : slots_) {
        if (slot.id == item.id && slot.meta == item.meta) {
            count -= slot.count;
            if (count <= 0) return true;
        }
    }
    return false;
}

std::unordered_map<int, item::ItemStack> BaseInventory::all(const item::ItemStack& item) {
    std::unordered_map<int, item::ItemStack> result;
    for (auto& [idx, slot] : slots_) {
        if (slot.id == item.id && slot.meta == item.meta) {
            result[idx] = slot;
        }
    }
    return result;
}

int BaseInventory::first(const item::ItemStack& item) {
    int count = std::max(1, item.count);
    for (auto& [idx, slot] : slots_) {
        if (slot.id == item.id && slot.meta == item.meta && slot.count >= count) {
            return idx;
        }
    }
    return -1;
}

int BaseInventory::firstEmpty(const item::ItemStack& /*item*/) {
    // getSize(), not size_ — see canAddItem's comment. Without this a
    // caller using firstEmpty() to decide where to place a new item
    // could get back an armor slot index.
    int limit = getSize();
    for (int i = 0; i < limit; ++i) {
        if (getItem(i).id == 0) return i;
    }
    return -1;
}

// NOTE (SIGSEGV): this used to walk `slots_` live and call clear(idx)
// inside the loop body. clear() does slots_.erase(index), which destroys
// the map node the loop's iterator points at, so the following `++it` was
// a use-after-free — dropping a stack from the hotbar killed the server.
// Collect the keys first (clearAll() below does the same) and only then
// erase, exactly like PHP's BaseInventory::remove() which iterates over
// getContents() — a *copy* of the slot map.
void BaseInventory::remove(const item::ItemStack& item) {
    std::vector<int> matches;
    matches.reserve(slots_.size());
    for (const auto& entry : slots_) {
        const item::ItemStack& slot = entry.second;
        if (slot.id == item.id && slot.meta == item.meta) {
            matches.push_back(entry.first);
        }
    }
    for (int idx : matches) clear(idx);
}

// ---- clear / clearAll ----
bool BaseInventory::clear(int index) {
    if (slots_.count(index)) {
        item::ItemStack old = slots_[index];
        slots_.erase(index);
        onSlotChange(index, old);
    }
    return true;
}

void BaseInventory::clearAll() {
    auto copy = slots_;
    for (auto& [idx, _] : copy) clear(idx);
}

// ---- onSlotChange ----
void BaseInventory::onSlotChange(int index, const item::ItemStack& /*before*/) {
    // Base implementation: no-op. PlayerInventory overrides to send
    // a ContainerSetSlotPacket back to the holder.
    (void)index;
}

// ---- sendContents / sendSlot ----
void BaseInventory::sendContents(Player* player) {
    if (!player) return;
    auto pk = std::make_shared<network::protocol::ContainerSetContentPacket>();
    pk->windowid = 0;
    pk->slots.reserve(size_);
    for (int i = 0; i < size_; ++i) {
        pk->slots.push_back(getItem(i));
    }
    player->sendPacket(pk);
}

void BaseInventory::sendSlot(int index, Player* player) {
    if (!player) return;
    auto pk = std::make_shared<network::protocol::ContainerSetSlotPacket>();
    pk->windowid = 0;
    pk->slot = static_cast<int16_t>(index);
    pk->hotbarSlot = static_cast<int16_t>(index);
    auto item = getItem(index);
    pk->itemId     = static_cast<int16_t>(item.id);
    pk->itemCount  = item.count;
    pk->itemDamage = static_cast<int16_t>(item.meta);
    // Same rule as PlayerInventory::sendSlot: the ack must echo the
    // stack's NBT ({display:{Name}}) or a custom-named item renders
    // nameless on the client after any slot update.
    pk->itemNbt    = item.nbt;
    player->sendPacket(pk);
}

} // namespace cursestone::inventory
