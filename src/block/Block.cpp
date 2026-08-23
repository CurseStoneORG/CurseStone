#include "Block.h"
#include <vector>
#include <memory>
namespace cenisyspe::block {
cenisyspe::math::AxisAlignedBB Block::getBoundingBox() const {
    return {0,0,0, 1,1,1};
}
std::vector<std::unique_ptr<Block>>& Block::registry() {
    static std::vector<std::unique_ptr<Block>> r;
    if (r.empty()) r.resize(256);
    return r;
}
void Block::registerBlock(std::unique_ptr<Block> b) {
    uint8_t id = b->id();
    registry()[id] = std::move(b);
}
Block* Block::get(uint8_t id) {
    auto& r = registry();
    return r[id].get();
}
} // namespace
