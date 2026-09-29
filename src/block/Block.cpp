#include "Block.h"
#include "BlockLightC.h"
#include <vector>
#include <memory>

// C bridge for the pure-C chunk/light layer (Chunk.c). Chunk.c cannot
// include C++ headers (see the "pure C" note in Chunk.h), so this is
// the one narrow crossing point: given a block id, how much sky/block
// light does it eat per step? Mirrors Minecraft's lightOpacity: 0 for
// air/leaves/glass/water etc (transparent), 15 (full block) for
// ordinary solids. Torch/glowstone light EMISSION is handled
// separately by mcp_block_light_emission below.
uint8_t mcp_block_light_opacity(uint8_t block_id) {
    if (block_id == 0) return 0; // air — fast path, avoids touching the registry
    cursestone::block::Block* b = cursestone::block::Block::get(block_id);
    if (!b) return 15; // unknown id — treat as solid, same fallback as Block::get
    return b->isTransparent() ? 0 : 15;
}

uint8_t mcp_block_light_emission(uint8_t block_id) {
    if (block_id == 0) return 0;
    cursestone::block::Block* b = cursestone::block::Block::get(block_id);
    if (!b) return 0;
    return b->getLightLevel();
}

namespace cursestone::block {
cursestone::math::AxisAlignedBB Block::getBoundingBox() const {
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
