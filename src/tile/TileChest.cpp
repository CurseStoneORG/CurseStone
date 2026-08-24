#include "TileChest.h"
#include "item/ItemStack.h"
namespace atomikpe::tile {
void TileChest::setItem(int /*slot*/, const atomikpe::item::ItemStack& /*it*/) {
    dirty_ = true;
}
atomikpe::item::ItemStack TileChest::getItem(int /*slot*/) const {
    return atomikpe::item::ItemStack{0,0,0};
}
void TileChest::removeItem(int /*slot*/) { dirty_ = true; }
void TileChest::writeNBT(class CompoundTag* /*tag*/) const {}
void TileChest::readNBT(const class CompoundTag* /*tag*/) {}
} // namespace
