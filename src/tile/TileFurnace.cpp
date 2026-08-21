#include "TileFurnace.h"
#include "item/ItemStack.h"
namespace mcppeserver::tile {
void TileFurnace::setItem(int /*slot*/, const mcppeserver::item::ItemStack& /*it*/) { dirty_ = true; }
mcppeserver::item::ItemStack TileFurnace::getItem(int /*slot*/) const {
    return mcppeserver::item::ItemStack{0,0,0};
}
void TileFurnace::removeItem(int /*slot*/) { dirty_ = true; }
void TileFurnace::tick(uint64_t) {
    if (burn_time_ > 0) {
        --burn_time_;
        if (++cook_time_ >= maxCookTime()) {
            cook_time_ = 0;
            // smelt result here (omitted for brevity)
        }
    }
    dirty_ = true;
}
} // namespace
