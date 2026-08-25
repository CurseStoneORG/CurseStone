#include "Inventory.h"
#include <algorithm>
namespace atomikpe::inventory {
atomikpe::item::ItemStack Inventory::getItem(int slot) const {
    if (slot < 0 || slot >= (int)items_.size()) return atomikpe::item::ItemStack::EMPTY();
    return items_[slot];
}
void Inventory::setItem(int slot, const atomikpe::item::ItemStack& it) {
    if (slot < 0 || slot >= (int)items_.size()) return;
    items_[slot] = it;
    if (on_slot_change_) on_slot_change_(slot, it);
}
void Inventory::clear(int slot) {
    if (slot < 0) for (auto& s : items_) s = atomikpe::item::ItemStack::EMPTY();
    else if (slot < (int)items_.size()) items_[slot] = atomikpe::item::ItemStack::EMPTY();
    if (on_slot_change_) on_slot_change_(slot, atomikpe::item::ItemStack::EMPTY());
}

bool Inventory::canAddItem(const atomikpe::item::ItemStack& item) const {
    if (item.isEmpty()) return true;
    int remaining = item.count;
    int max_stack = maxSize();
    // First pass: try to stack into existing same-id/same-meta slots.
    for (int i = 0; i < (int)items_.size() && remaining > 0; ++i) {
        const auto& slot = items_[i];
        if (slot.id == item.id && slot.meta == item.meta && slot.count > 0) {
            int diff = max_stack - slot.count;
            if (diff > 0) remaining -= diff;
        }
    }
    if (remaining <= 0) return true;
    // Second pass: any empty slot can take up to max_stack units.
    for (int i = 0; i < (int)items_.size() && remaining > 0; ++i) {
        const auto& slot = items_[i];
        if (slot.isEmpty()) {
            remaining -= max_stack;
        }
    }
    return remaining <= 0;
}

atomikpe::item::ItemStack Inventory::addItem(atomikpe::item::ItemStack item) {
    if (item.isEmpty()) return atomikpe::item::ItemStack::EMPTY();
    int max_stack = maxSize();
    // Collect empty-slot indices for the second pass.
    std::vector<int> empty_slots;
    // First pass: stack into existing same-id/same-meta slots.
    for (int i = 0; i < (int)items_.size() && item.count > 0; ++i) {
        auto& slot = items_[i];
        if (slot.isEmpty()) { empty_slots.push_back(i); continue; }
        if (slot.id == item.id && slot.meta == item.meta && slot.count < max_stack) {
            int amount = std::min(max_stack - slot.count, item.count);
            slot.count += amount;
            item.count -= amount;
            if (on_slot_change_) on_slot_change_(i, slot);
        }
    }
    // Second pass: fill empty slots.
    for (int idx : empty_slots) {
        if (item.count <= 0) break;
        int amount = std::min(max_stack, item.count);
        items_[idx] = item;
        items_[idx].count = amount;
        item.count -= amount;
        if (on_slot_change_) on_slot_change_(idx, items_[idx]);
    }
    // Return leftover.
    if (item.count <= 0) return atomikpe::item::ItemStack::EMPTY();
    return item;
}
} // namespace
