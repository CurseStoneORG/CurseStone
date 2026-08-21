#include "Inventory.h"
namespace mcppeserver::inventory {
mcppeserver::item::ItemStack Inventory::getItem(int slot) const {
    if (slot < 0 || slot >= (int)items_.size()) return mcppeserver::item::ItemStack::EMPTY();
    return items_[slot];
}
void Inventory::setItem(int slot, const mcppeserver::item::ItemStack& it) {
    if (slot < 0 || slot >= (int)items_.size()) return;
    items_[slot] = it;
    if (on_slot_change_) on_slot_change_(slot, it);
}
void Inventory::clear(int slot) {
    if (slot < 0) for (auto& s : items_) s = mcppeserver::item::ItemStack::EMPTY();
    else if (slot < (int)items_.size()) items_[slot] = mcppeserver::item::ItemStack::EMPTY();
    if (on_slot_change_) on_slot_change_(slot, mcppeserver::item::ItemStack::EMPTY());
}
} // namespace
