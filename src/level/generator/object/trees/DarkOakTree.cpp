// SPDX-License-Identifier: LGPL-3.0-or-later
#include "DarkOakTree.h"
#include "block/BlockIds.h"
#include <cmath>
#include <cstdlib>

namespace cursestone::level::generator::object::trees {

using cursestone::block::BlockId;

void DarkOakTree::init(GenRandom& rng) {
    height_ = (int)(rng.nextInt(2) + rng.nextInt(3)) + 6;   // 6..10 (reference)
    logId_ = 162;  logData_ = 1;   // WOOD_2, dark oak
    leafId_ = 161; leafData_ = 1;  // LEAVES_2, dark oak
}

bool DarkOakTree::canPlaceOn(uint8_t soil) const {
    return soil == (uint8_t)BlockId::GRASS || soil == (uint8_t)BlockId::DIRT;
}

void DarkOakTree::canopy(mcp_chunk_t* c, int cx, int trunkTopY, int cz, GenRandom& rng) {
    // Reference DarkOakTree::Generate leaf block (symmetric in x/z).
    for (int x = -2; x <= 0; ++x) {
        for (int z = -2; z <= 0; ++z) {
            if ((x != -1 || z != -2) && (x > -2 || z > -1)) {
                setLeaf(c, cx + x, trunkTopY + 1, cz + z, leafId_, leafData_);
                setLeaf(c, 1 + cx - x, trunkTopY + 1, cz + z, leafId_, leafData_);
                setLeaf(c, cx + x, trunkTopY + 1, 1 + cz - z, leafId_, leafData_);
                setLeaf(c, 1 + cx - x, trunkTopY + 1, 1 + cz - z, leafId_, leafData_);
            }
            setLeaf(c, cx + x, trunkTopY - 1, cz + z, leafId_, leafData_);
            setLeaf(c, 1 + cx - x, trunkTopY - 1, cz + z, leafId_, leafData_);
            setLeaf(c, cx + x, trunkTopY - 1, 1 + cz - z, leafId_, leafData_);
            setLeaf(c, 1 + cx - x, trunkTopY - 1, 1 + cz - z, leafId_, leafData_);
        }
    }
    // Flat layer that finishes the canopy just under the trunk top.
    for (int x = -3; x <= 4; ++x) {
        for (int z = -3; z <= 4; ++z) {
            if (std::abs(x) < 3 || std::abs(z) < 3)
                setLeaf(c, cx + x, trunkTopY, cz + z, leafId_, leafData_);
        }
    }
    // 50% chance of a small 2x2 cap on top.
    if (rng.nextInt(2) == 0) {
        setLeaf(c, cx, trunkTopY + 2, cz, leafId_, leafData_);
        setLeaf(c, cx + 1, trunkTopY + 2, cz, leafId_, leafData_);
        setLeaf(c, cx + 1, trunkTopY + 2, cz + 1, leafId_, leafData_);
        setLeaf(c, cx, trunkTopY + 2, cz + 1, leafId_, leafData_);
    }
}

bool DarkOakTree::generate(mcp_chunk_t* c, int sourceX, int sourceY,
                           int sourceZ, GenRandom& rng) {
    init(rng);
    if (sourceY < 2 || sourceY > 124) return false;
    if (!inXZ(sourceX) || !inXZ(sourceZ)) return false;
    uint8_t soil = mcp_chunk_get_block_id(c, sourceX, sourceY - 1, sourceZ);
    if (!canPlaceOn(soil)) return false;

    float d = rng.nextFloat() * (float)(M_PI * 2.0);
    int dx = (int)(std::cos(d) + 1.5F) - 1;
    int dz = (int)(std::sin(d) + 1.5F) - 1;
    if (std::abs(dx) > 0 && std::abs(dz) > 0) {
        if (rng.nextBoolean()) dx = 0; else dz = 0;
    }

    int twistHeight = height_ - (int)rng.nextInt(4);
    int twistCount = (int)rng.nextInt(3);
    int centerX = sourceX, centerZ = sourceZ, trunkTopY = 0;

    // 2x2 twisting trunk.
    for (int y = 0; y < height_; ++y) {
        if (twistCount > 0 && y >= twistHeight) {
            centerX += dx;
            centerZ += dz;
            --twistCount;
        }
        if (!replaceable(mcp_chunk_get_block_id(c, centerX, sourceY + y, centerZ)))
            continue;
        trunkTopY = sourceY + y;
        setLog(c, centerX, sourceY + y, centerZ, logId_, logData_);
        setLog(c, centerX, sourceY + y, centerZ + 1, logId_, logData_);
        setLog(c, centerX + 1, sourceY + y, centerZ, logId_, logData_);
        setLog(c, centerX + 1, sourceY + y, centerZ + 1, logId_, logData_);
    }

    canopy(c, centerX, trunkTopY, centerZ, rng);

    // Soil under the whole 2x2 trunk becomes dirt.
    for (int i = 0; i <= 1; ++i) {
        for (int j = 0; j <= 1; ++j) {
            uint8_t b = mcp_chunk_get_block_id(c, sourceX + i, sourceY - 1, sourceZ + j);
            if (b == (uint8_t)BlockId::GRASS || b == (uint8_t)BlockId::DIRT)
                setForced(c, sourceX + i, sourceY - 1, sourceZ + j, (uint8_t)BlockId::DIRT, 0);
        }
    }
    return true;
}

} // namespace cursestone::level::generator::object::trees
