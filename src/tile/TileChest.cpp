#include "TileChest.h"
#include "item/ItemStack.h"
namespace mcppeserver::tile {
void TileChest::setItem(int /*slot*/, const mcppeserver::item::ItemStack& /*it*/) {
    dirty_ = true;
}
mcppeserver::item::ItemStack TileChest::getItem(int /*slot*/) const {
    return mcppeserver::item::ItemStack{0,0,0};
}
void TileChest::removeItem(int /*slot*/) { dirty_ = true; }
void TileChest::writeNBT(class CompoundTag* /*tag*/) const {}
void TileChest::readNBT(const class CompoundTag* /*tag*/) {}
} // namespace
