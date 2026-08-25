#include "Inventory.h"
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
} // namespace
