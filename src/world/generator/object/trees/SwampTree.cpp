// SPDX-License-Identifier: LGPL-3.0-or-later
#include "SwampTree.h"
#include "block/BlockIds.h"
#include <cstdlib>

namespace cursestone::level::generator::object::trees {

using cursestone::block::BlockId;

void SwampTree::init(GenRandom& rng) {
    height_ = 5 + (int)rng.nextInt(4);   // 5..8 (reference SwampTree)
    logId_ = 17;  logData_ = 0;   // LOG, oak
    leafId_ = 18; leafData_ = 0;  // LEAVES, oak
}

bool SwampTree::canPlaceOn(uint8_t soil) const {
    return soil == (uint8_t)BlockId::GRASS || soil == (uint8_t)BlockId::DIRT;
}

// Reference SwampTree: same layer loop as the oak but with radius
// 2 - n/2 (so a flatter, wider, drooping canopy).
void SwampTree::placeCanopy(mcp_chunk_t* c, int sourceX, int sourceY,
                            int sourceZ, GenRandom& rng) {
    for (int y = sourceY + height_ - 3; y <= sourceY + height_; ++y) {
        int n = y - (sourceY + height_);
        int radius = 2 - n / 2;
        for (int x = sourceX - radius; x <= sourceX + radius; ++x) {
            for (int z = sourceZ - radius; z <= sourceZ + radius; ++z) {
                if (std::abs(x - sourceX) != radius || std::abs(z - sourceZ) != radius ||
                    (rng.nextBoolean() && n != 0)) {
                    setLeaf(c, x, y, z, leafId_, leafData_);
                }
            }
        }
    }
}

// The swamp trunk may be immersed one block in water, so it replaces
// water as well as air/leaves.
void SwampTree::placeTrunk(mcp_chunk_t* c, int sourceX, int sourceY,
                           int sourceZ, GenRandom&) {
    for (int i = 0; i < height_; ++i) {
        int y = sourceY + i;
        if (!inXZ(sourceX) || !inXZ(sourceZ) || !inY(y)) continue;
        uint8_t cur = mcp_chunk_get_block_id(c, sourceX, y, sourceZ);
        bool ok = replaceable(cur) ||
                  cur == (uint8_t)BlockId::WATER || cur == (uint8_t)BlockId::FLOWING_WATER;
        if (ok) setLog(c, sourceX, y, sourceZ, logId_, logData_);
    }
}

} // namespace cursestone::level::generator::object::trees
