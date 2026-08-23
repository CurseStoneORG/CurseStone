#include "Inventory.h"
namespace cenisyspe::inventory {
cenisyspe::item::ItemStack Inventory::getItem(int slot) const {
    if (slot < 0 || slot >= (int)items_.size()) return cenisyspe::item::ItemStack::EMPTY();
    return items_[slot];
}
void Inventory::setItem(int slot, const cenisyspe::item::ItemStack& it) {
    if (slot < 0 || slot >= (int)items_.size()) return;
    items_[slot] = it;
    if (on_slot_change_) on_slot_change_(slot, it);
}
void Inventory::clear(int slot) {
    if (slot < 0) for (auto& s : items_) s = cenisyspe::item::ItemStack::EMPTY();
    else if (slot < (int)items_.size()) items_[slot] = cenisyspe::item::ItemStack::EMPTY();
    if (on_slot_change_) on_slot_change_(slot, cenisyspe::item::ItemStack::EMPTY());
}
} // namespace
