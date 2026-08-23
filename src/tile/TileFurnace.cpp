#include "TileFurnace.h"
#include "item/ItemStack.h"
namespace cenisyspe::tile {
void TileFurnace::setItem(int /*slot*/, const cenisyspe::item::ItemStack& /*it*/) { dirty_ = true; }
cenisyspe::item::ItemStack TileFurnace::getItem(int /*slot*/) const {
    return cenisyspe::item::ItemStack{0,0,0};
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
