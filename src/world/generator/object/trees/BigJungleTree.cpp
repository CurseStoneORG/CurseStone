// SPDX-License-Identifier: LGPL-3.0-or-later
#include "BigJungleTree.h"
#include "block/BlockIds.h"
#include <cmath>

namespace cursestone::level::generator::object::trees {

using cursestone::block::BlockId;

namespace {

// 2*pi. Spelled out rather than M_PI because M_PI is not guaranteed by the
// standard headers on MSVC, and this project builds there too.
constexpr double kTau = 6.283185307179586476925286766559;

// HugeTree::getHeight() -> baseHeight + rand->nextRange(0, extraRandom - 1).
// These are the values the reference jungle populator constructs the tree
// with (base 15, 10 blocks of randomness), giving a 15..24 block tree.
constexpr int kBaseHeight  = 15;
constexpr int kExtraRandom = 10;

} // namespace

void BigJungleTree::init(GenRandom& rng) {
    height_ = kBaseHeight + (int)rng.nextInt(kExtraRandom);   // 15..24
    logId_  = (uint8_t)BlockId::LOG;
    logData_  = 3;      // jungle wood
    leafId_ = (uint8_t)BlockId::LEAVES;
    leafData_ = 3;      // jungle leaves
}

bool BigJungleTree::canOverrideAt(mcp_chunk_t* c, int x, int y, int z) {
    // Outside the chunk there is nothing to override: the reference walks
    // absolute coordinates and the block simply lives in the neighbour.
    if (!inXZ(x) || !inXZ(z) || !inY(y)) return true;
    switch (mcp_chunk_get_block_id(c, x, y, z)) {
        case (uint8_t)BlockId::AIR:
        case (uint8_t)BlockId::LEAVES:
        case (uint8_t)BlockId::LEAVES_2:
        case (uint8_t)BlockId::LOG:
        case (uint8_t)BlockId::WOOD_2:
        case (uint8_t)BlockId::TALL_GRASS:
        case (uint8_t)BlockId::DOUBLE_PLANT:
        case (uint8_t)BlockId::DEAD_BUSH:
        case (uint8_t)BlockId::FLOWER_YELLOW:
        case (uint8_t)BlockId::FLOWER_RED:
        case (uint8_t)BlockId::VINE:
        case (uint8_t)BlockId::SNOW_LAYER:
            return true;
        default:
            return false;
    }
}

void BigJungleTree::growLeavesLayer(mcp_chunk_t* c, int x, int y, int z,
                                    int radius, GenRandom& rng) {
    for (int dx = radius; dx >= -radius; --dx) {
        for (int dz = radius; dz >= -radius; --dz) {
            if (dx != 0 && dz != 0 && rng.nextBoolean()) continue;  // cull corner
            setForced(c, x + dx, y, z + dz, leafId_, leafData_);
        }
    }
}

void BigJungleTree::growLeavesLayerStrict(mcp_chunk_t* c, int x, int y, int z,
                                          int radius) {
    for (int dx = -radius; dx <= radius; ++dx)
        for (int dz = -radius; dz <= radius; ++dz)
            setForced(c, x + dx, y, z + dz, leafId_, leafData_);
}

void BigJungleTree::placeVine(mcp_chunk_t* c, GenRandom& rng, int x, int y,
                              int z, uint8_t faceMask) {
    // The reference draws the random number BEFORE testing the block, and
    // that order matters: it is what makes the vine pattern reproducible.
    if (rng.nextInt(3) > 0 && inXZ(x) && inXZ(z) && inY(y) &&
        mcp_chunk_get_block_id(c, x, y, z) == (uint8_t)BlockId::AIR) {
        setForced(c, x, y, z, (uint8_t)BlockId::VINE, faceMask);
    }
}

bool BigJungleTree::generate(mcp_chunk_t* c, int sourceX, int sourceY,
                             int sourceZ, uint8_t soilId, GenRandom& rng) {
    init(rng);

    // ensureGrowable(): room for the whole tree plus the crown above it,
    // and ground the species accepts.
    if (sourceY < 2 || sourceY > 124 - height_) return false;
    if (!canPlaceOn(soilId)) return false;

    const int   height = height_;
    const int   baseY  = sourceY;
    const double baseX = (double)sourceX;
    const double baseZ = (double)sourceZ;

    // ---- createCrown(position->up(height), 2) --------------------------
    // Three solid layers of radius 3, 2 and 1 at the top of the trunk.
    const int crownX = (int)std::floor(baseX);
    const int crownZ = (int)std::floor(baseZ);
    for (int j = -2; j <= 0; ++j)
        growLeavesLayerStrict(c, crownX, baseY + height + j, crownZ, 2 + 1 - j);

    // ---- branches -------------------------------------------------------
    // Walking up from just below the crown, each branch throws a wooden
    // arm out at a random bearing and caps it with a blob of leaves.
    // PHP's $k/$l are FLOATS and Vector3 floors them, so the branch tip and
    // its leaf cluster share the i1 == 4 value of the inner loop.
    int tipX = crownX, tipZ = crownZ;
    for (int j = baseY + height - 2 - (int)rng.nextInt(4);
         j > baseY + height / 2;
         j -= 2 + (int)rng.nextInt(4)) {
        const double f = rng.nextFloat() * kTau;
        for (int i1 = 0; i1 < 5; ++i1) {
            tipX = (int)std::floor(baseX + (1.5 + std::cos(f) * (double)i1));
            tipZ = (int)std::floor(baseZ + (1.5 + std::sin(f) * (double)i1));
            const int ly = (int)std::floor((double)(j - 3) + (double)i1 / 2.0);
            setForced(c, tipX, ly, tipZ, logId_, logData_);
        }
        const int j2 = 1 + (int)rng.nextInt(2);
        const int j1 = j;
        for (int k1 = j - j2; k1 <= j1; ++k1) {
            const int l1 = k1 - j1;
            growLeavesLayer(c, tipX, k1, tipZ, 1 - l1, rng);
        }
    }

    // ---- trunk, buttresses and vines ------------------------------------
    // The trunk is a single column; below the very top it is flanked by an
    // east, a south-east and a south log, which is the buttressed base the
    // species is known for. Every wood block that can be overridden also
    // drops vines on two of its faces.
    for (int i2 = 0; i2 < height; ++i2) {
        const int by = baseY + i2;
        if (canOverrideAt(c, sourceX, by, sourceZ)) {
            setForced(c, sourceX, by, sourceZ, logId_, logData_);
            if (i2 > 0) {
                placeVine(c, rng, sourceX - 1, by, sourceZ, 8);  // west
                placeVine(c, rng, sourceX,     by, sourceZ - 1, 1); // north
            }
        }

        if (i2 < height - 1) {
            const int ex = sourceX + 1;
            if (canOverrideAt(c, ex, by, sourceZ)) {
                setForced(c, ex, by, sourceZ, logId_, logData_);
                if (i2 > 0) {
                    placeVine(c, rng, ex + 1, by, sourceZ,     2);
                    placeVine(c, rng, ex,     by, sourceZ - 1, 1);
                }
            }

            const int sx = sourceX + 1, sz = sourceZ + 1;   // south-east
            if (canOverrideAt(c, sx, by, sz)) {
                setForced(c, sx, by, sz, logId_, logData_);
                if (i2 > 0) {
                    placeVine(c, rng, sx + 1, by, sz,     2);
                    placeVine(c, rng, sx,     by, sz + 1, 4);
                }
            }

            const int sz3 = sourceZ + 1;                     // south
            if (canOverrideAt(c, sourceX, by, sz3)) {
                setForced(c, sourceX, by, sz3, logId_, logData_);
                if (i2 > 0) {
                    placeVine(c, rng, sourceX - 1, by, sz3,     8);
                    placeVine(c, rng, sourceX,     by, sz3 + 1, 4);
                }
            }
        }
    }

    // The block right under the trunk always becomes dirt (reference).
    if (inXZ(sourceX) && inXZ(sourceZ) && inY(baseY - 1)) {
        const uint8_t below = mcp_chunk_get_block_id(c, sourceX, baseY - 1, sourceZ);
        if (below == (uint8_t)BlockId::GRASS || below == (uint8_t)BlockId::DIRT)
            setForced(c, sourceX, baseY - 1, sourceZ, (uint8_t)BlockId::DIRT, 0);
    }
    return true;
}

} // namespace cursestone::level::generator::object::trees