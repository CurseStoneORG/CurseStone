#include "TileChest.h"
#include "item/ItemStack.h"
namespace atomikpe::tile {
void TileChest::setItem(int slot, const atomikpe::item::ItemStack& it) {
    if (slot < 0 || slot >= SIZE) return;
    inventory_->setItem(slot, it);
    dirty_ = true;
}
atomikpe::item::ItemStack TileChest::getItem(int slot) const {
    if (slot < 0 || slot >= SIZE) return atomikpe::item::ItemStack::EMPTY();
    return inventory_->getItem(slot);
}
void TileChest::removeItem(int slot) {
    if (slot < 0 || slot >= SIZE) return;
    inventory_->clear(slot);
    dirty_ = true;
}
void TileChest::writeNBT(class CompoundTag* /*tag*/) const {}
void TileChest::readNBT(const class CompoundTag* /*tag*/) {}
} // namespace
