#include "TileFurnace.h"
#include "item/ItemStack.h"
namespace atomikpe::tile {
void TileFurnace::setItem(int /*slot*/, const atomikpe::item::ItemStack& /*it*/) { dirty_ = true; }
atomikpe::item::ItemStack TileFurnace::getItem(int /*slot*/) const {
    return atomikpe::item::ItemStack{0,0,0};
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
