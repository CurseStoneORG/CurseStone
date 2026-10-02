// SPDX-License-Identifier: LGPL-3.0-or-later
#include "AcaciaTree.h"
#include "block/BlockIds.h"
#include <cmath>
#include <cstdlib>

namespace cursestone::level::generator::object::trees {

using cursestone::block::BlockId;

void AcaciaTree::init(GenRandom& rng) {
    height_ = (int)(rng.nextInt(3) + rng.nextInt(3)) + 5;   // reference
    logId_ = 162; logData_ = 0;    // WOOD_2, acacia
    leafId_ = 161; leafData_ = 0;  // LEAVES_2, acacia
}

bool AcaciaTree::canPlaceOn(uint8_t soil) const {
    return soil == (uint8_t)BlockId::GRASS || soil == (uint8_t)BlockId::DIRT;
}

void AcaciaTree::canopy(mcp_chunk_t* c, int cx, int trunkTopY, int cz,
                        GenRandom&, int /*spread*/) {
    // Reference AcaciaTree::Generate leaf block:
    //   at trunkTopY    : square minus corners (abs(x)<3 || abs(z)<3)
    //   at trunkTopY+1  : inner 3x3 plus the 4 edge middles
    for (int x = -3; x <= 3; ++x) {
        for (int z = -3; z <= 3; ++z) {
            if (std::abs(x) < 3 || std::abs(z) < 3)
                setLeaf(c, cx + x, trunkTopY, cz + z, leafId_, leafData_);
            if (std::abs(x) < 2 && std::abs(z) < 2)
                setLeaf(c, cx + x, trunkTopY + 1, cz + z, leafId_, leafData_);
            if ((std::abs(x) == 2 && std::abs(z) == 0) ||
                (std::abs(x) == 0 && std::abs(z) == 2))
                setLeaf(c, cx + x, trunkTopY + 1, cz + z, leafId_, leafData_);
        }
    }
}

bool AcaciaTree::generate(mcp_chunk_t* c, int sourceX, int sourceY,
                          int sourceZ, GenRandom& rng) {
    init(rng);
    if (sourceY < 2 || sourceY > 124) return false;
    if (!inXZ(sourceX) || !inXZ(sourceZ)) return false;
    uint8_t soil = mcp_chunk_get_block_id(c, sourceX, sourceY - 1, sourceZ);
    if (!canPlaceOn(soil)) return false;

    // Direction of the trunk twist (reduced to NESW).
    float d = rng.nextFloat() * (float)(M_PI * 2.0);
    int dx = (int)(std::cos(d) + 1.5F) - 1;
    int dz = (int)(std::sin(d) + 1.5F) - 1;
    if (std::abs(dx) > 0 && std::abs(dz) > 0) {
        if (rng.nextBoolean()) dx = 0; else dz = 0;
    }

    int twistHeight = height_ - 1 - (int)rng.nextInt(4);
    int twistCount = (int)rng.nextInt(3) + 1;
    int centerX = sourceX, centerZ = sourceZ, trunkTopY = 0;

    for (int y = 0; y < height_; ++y) {
        if (twistCount > 0 && y >= twistHeight) {
            centerX += dx;
            centerZ += dz;
            --twistCount;
        }
        if (replaceable(mcp_chunk_get_block_id(c, centerX, sourceY + y, centerZ))) {
            trunkTopY = sourceY + y;
            setLog(c, centerX, sourceY + y, centerZ, logId_, logData_);
        }
    }
    canopy(c, centerX, trunkTopY, centerZ, rng, 3);

    // Optional second branch in a different direction.
    d = rng.nextFloat() * (float)(M_PI * 2.0);
    int dxB = (int)(std::cos(d) + 1.5F) - 1;
    int dzB = (int)(std::sin(d) + 1.5F) - 1;
    if (std::abs(dxB) > 0 && std::abs(dzB) > 0) {
        if (rng.nextBoolean()) dxB = 0; else dzB = 0;
    }

    if (dx != dxB || dz != dzB) {
        centerX = sourceX; centerZ = sourceZ;
        int branchHeight = twistHeight - 1 - (int)rng.nextInt(2);
        twistCount = (int)rng.nextInt(3) + 1;
        trunkTopY = 0;
        for (int y = branchHeight + 1; y < height_; ++y) {
            if (twistCount > 0) {
                centerX += dxB;
                centerZ += dzB;
                if (replaceable(mcp_chunk_get_block_id(c, centerX, sourceY + y, centerZ))) {
                    trunkTopY = sourceY + y;
                    setLog(c, centerX, sourceY + y, centerZ, logId_, logData_);
                }
                --twistCount;
            }
        }
        if (trunkTopY > 0) {
            for (int x = -2; x <= 2; ++x)
                for (int z = -2; z <= 2; ++z)
                    if (std::abs(x) < 2 || std::abs(z) < 2)
                        setLeaf(c, centerX + x, trunkTopY, centerZ + z, leafId_, leafData_);
            for (int x = -1; x <= 1; ++x)
                for (int z = -1; z <= 1; ++z)
                    setLeaf(c, centerX + x, trunkTopY + 1, centerZ + z, leafId_, leafData_);
        }
    }

    uint8_t below = mcp_chunk_get_block_id(c, sourceX, sourceY - 1, sourceZ);
    if (below == (uint8_t)BlockId::GRASS || below == (uint8_t)BlockId::DIRT)
        setForced(c, sourceX, sourceY - 1, sourceZ, (uint8_t)BlockId::DIRT, 0);
    return true;
}

} // namespace cursestone::level::generator::object::trees
