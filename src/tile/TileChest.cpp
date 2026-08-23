#include "TileChest.h"
#include "item/ItemStack.h"
namespace cenisyspe::tile {
void TileChest::setItem(int /*slot*/, const cenisyspe::item::ItemStack& /*it*/) {
    dirty_ = true;
}
cenisyspe::item::ItemStack TileChest::getItem(int /*slot*/) const {
    return cenisyspe::item::ItemStack{0,0,0};
}
void TileChest::removeItem(int /*slot*/) { dirty_ = true; }
void TileChest::writeNBT(class CompoundTag* /*tag*/) const {}
void TileChest::readNBT(const class CompoundTag* /*tag*/) {}
} // namespace
