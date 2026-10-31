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
#include "DoubleChestInventory.h"
#include "Player.h"
namespace cursestone::inventory {

DoubleChestInventory::DoubleChestInventory(std::shared_ptr<ChestInventory> left,
                                           std::shared_ptr<ChestInventory> right)
    : BaseInventory(nullptr, InventoryType::get(InventoryType::DOUBLE_CHEST)),
      left_(std::move(left)),
      right_(std::move(right)) {
    // BaseInventory already picked up the DOUBLE_CHEST type (54 slots,
    // "Double Chest") — nothing else to seed: the wrapper never touches
    // BaseInventory::slots_, it always forwards to the two halves.
}

item::ItemStack DoubleChestInventory::getItem(int index) {
    if (index < 0 || index >= size_)
        return item::ItemStack::EMPTY();
    if (index < HALF)
        return left_ ? left_->getItem(index) : item::ItemStack::EMPTY();
    return right_ ? right_->getItem(index - HALF) : item::ItemStack::EMPTY();
}

bool DoubleChestInventory::setItem(int index, const item::ItemStack& item) {
    if (index < 0 || index >= size_)
        return false;
    if (index < HALF)
        return left_ ? left_->setItem(index, item) : false;
    return right_ ? right_->setItem(index - HALF, item) : false;
}

std::vector<item::ItemStack> DoubleChestInventory::addItem(std::vector<item::ItemStack> slots) {
    // Fill the left half first, then the right half — the same fill order
    // the vanilla large chest produces for hopper/automation insertions.
    if (!left_)
        return right_ ? right_->addItem(std::move(slots)) : slots;
    auto leftovers = left_->addItem(std::move(slots));
    if (leftovers.empty() || !right_)
        return leftovers;
    return right_->addItem(std::move(leftovers));
}

bool DoubleChestInventory::canAddItem(const item::ItemStack& item) {
    return (left_ && left_->canAddItem(item)) || (right_ && right_->canAddItem(item));
}

std::vector<item::ItemStack> DoubleChestInventory::removeItem(std::vector<item::ItemStack> slots) {
    if (!left_)
        return right_ ? right_->removeItem(std::move(slots)) : slots;
    auto leftovers = left_->removeItem(std::move(slots));
    if (leftovers.empty() || !right_)
        return leftovers;
    return right_->removeItem(std::move(leftovers));
}

std::unordered_map<int, item::ItemStack> DoubleChestInventory::getContents() {
    std::unordered_map<int, item::ItemStack> out;
    out.reserve((size_t)size_);
    if (left_) {
        for (auto& [slot, stack] : left_->getContents()) {
            if (!stack.isEmpty() && slot >= 0 && slot < HALF)
                out[slot] = stack;
        }
    }
    if (right_) {
        for (auto& [slot, stack] : right_->getContents()) {
            if (!stack.isEmpty() && slot >= 0 && slot < HALF)
                out[slot + HALF] = stack;
        }
    }
    return out;
}

void DoubleChestInventory::setContents(const std::unordered_map<int, item::ItemStack>& items) {
    clearAll();
    for (const auto& [slot, stack] : items) {
        if (!stack.isEmpty())
            setItem(slot, stack);
    }
}

bool DoubleChestInventory::contains(const item::ItemStack& item) {
    return (left_ && left_->contains(item)) || (right_ && right_->contains(item));
}

std::unordered_map<int, item::ItemStack> DoubleChestInventory::all(const item::ItemStack& item) {
    auto out = left_ ? left_->all(item) : std::unordered_map<int, item::ItemStack>{};
    if (right_) {
        for (auto& [slot, stack] : right_->all(item))
            out[slot + HALF] = stack;
    }
    return out;
}

int DoubleChestInventory::first(const item::ItemStack& item) {
    int li = left_ ? left_->first(item) : -1;
    if (li >= 0)
        return li;
    int ri = right_ ? right_->first(item) : -1;
    return ri >= 0 ? ri + HALF : -1;
}

int DoubleChestInventory::firstEmpty(const item::ItemStack& item) {
    int li = left_ ? left_->firstEmpty(item) : -1;
    if (li >= 0)
        return li;
    int ri = right_ ? right_->firstEmpty(item) : -1;
    return ri >= 0 ? ri + HALF : -1;
}

void DoubleChestInventory::remove(const item::ItemStack& item) {
    if (left_)
        left_->remove(item);
    if (right_)
        right_->remove(item);
}

bool DoubleChestInventory::clear(int index) {
    if (index < 0 || index >= size_)
        return false;
    if (index < HALF)
        return left_ ? left_->clear(index) : false;
    return right_ ? right_->clear(index - HALF) : false;
}

void DoubleChestInventory::clearAll() {
    if (left_)
        left_->clearAll();
    if (right_)
        right_->clearAll();
}

void DoubleChestInventory::sendContents(Player* /*player*/) {
}

void DoubleChestInventory::sendSlot(int /*index*/, Player* /*player*/) {
}

} // namespace cursestone::inventory
