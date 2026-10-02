// SPDX-License-Identifier: LGPL-3.0-or-later
#include "level/generator/object/TreeGenerator.h"
#include "block/BlockIds.h"
#include <cstdlib>

namespace cursestone::level::generator::object {

using cursestone::block::BlockId;

namespace {

constexpr int kMaxY = 127; // MCPE 0.15.x world height

inline bool inXZ(int v) { return v >= 0 && v < 16; }

// Blocks a tree may overwrite (air + thin vegetation + leaves).
inline bool replaceable(uint8_t id) {
    switch (id) {
        case (uint8_t)BlockId::AIR:
        case (uint8_t)BlockId::LEAVES:
        case (uint8_t)BlockId::LEAVES_2:
        case (uint8_t)BlockId::TALL_GRASS:
        case (uint8_t)BlockId::DEAD_BUSH:
        case (uint8_t)BlockId::FLOWER_YELLOW:
        case (uint8_t)BlockId::FLOWER_RED:
        case (uint8_t)BlockId::BROWN_MUSHROOM:
        case (uint8_t)BlockId::RED_MUSHROOM:
        case (uint8_t)BlockId::SNOW_LAYER:
        case (uint8_t)BlockId::VINE:
            return true;
        default:
            return false;
    }
}

void setLeaf(mcp_chunk_t* c, int x, int y, int z, uint8_t id, uint8_t data) {
    if (!inXZ(x) || !inXZ(z) || y < 0 || y > kMaxY) return;
    if (replaceable(mcp_chunk_get_block_id(c, x, y, z)))
        mcp_chunk_set_block_full(c, x, y, z, id, data);
}

void setLog(mcp_chunk_t* c, int x, int y, int z, uint8_t id, uint8_t data) {
    if (!inXZ(x) || !inXZ(z) || y < 0 || y > kMaxY) return;
    mcp_chunk_set_block_full(c, x, y, z, id, data);
}

// Classic leaf blob used by oak/birch/swamp: radius 2 at the bottom of the
// canopy tapering to 1 at the top, corners optionally culled (see the
// reference's GenericTree::Generate).
void genericCanopy(mcp_chunk_t* c, int x, int y, int z, int height,
                   uint8_t leaf, uint8_t leafData, GenRandom& rng, bool droop) {
    for (int ly = y + height - 3; ly <= y + height; ++ly) {
        int n = ly - (y + height);
        int radius = (int)(1 - n / 2);
        for (int dx = -radius; dx <= radius; ++dx) {
            for (int dz = -radius; dz <= radius; ++dz) {
                if (std::abs(dx) != radius || std::abs(dz) != radius ||
                    (rng.nextBoolean() && n != 0)) {
                    setLeaf(c, x + dx, ly, z + dz, leaf, leafData);
                }
            }
        }
    }
    if (droop) {
        // Swamp trees hang one extra layer of leaves around the trunk.
        for (int dx = -2; dx <= 2; ++dx)
            for (int dz = -2; dz <= 2; ++dz)
                if (dx * dx + dz * dz <= 5)
                    setLeaf(c, x + dx, y + height - 3, z + dz, leaf, leafData);
    }
}

void placeOak(mcp_chunk_t* c, int x, int y, int z, GenRandom& rng) {
    int h = 4 + (int)rng.nextInt(3);
    genericCanopy(c, x, y, z, h, (uint8_t)BlockId::LEAVES, 0, rng, false);
    for (int i = 0; i < h; ++i) setLog(c, x, y + i, z, (uint8_t)BlockId::LOG, 0);
}

void placeBirch(mcp_chunk_t* c, int x, int y, int z, GenRandom& rng) {
    int h = 5 + (int)rng.nextInt(3);
    genericCanopy(c, x, y, z, h, (uint8_t)BlockId::LEAVES, 2, rng, false);
    for (int i = 0; i < h; ++i) setLog(c, x, y + i, z, (uint8_t)BlockId::LOG, 2);
}

void placeSwamp(mcp_chunk_t* c, int x, int y, int z, GenRandom& rng) {
    int h = 4 + (int)rng.nextInt(3);
    genericCanopy(c, x, y, z, h, (uint8_t)BlockId::LEAVES, 0, rng, true);
    for (int i = 0; i < h; ++i) setLog(c, x, y + i, z, (uint8_t)BlockId::LOG, 0);
}

void placeSpruce(mcp_chunk_t* c, int x, int y, int z, GenRandom& rng) {
    int h = 6 + (int)rng.nextInt(5);
    for (int i = 0; i < h; ++i) setLog(c, x, y + i, z, (uint8_t)BlockId::LOG, 1);
    int top = y + h;
    for (int ly = top; ly > y + 1; --ly) {
        int d = top - ly;
        int radius = d / 3 + 1;
        if (radius > 3) radius = 3;
        for (int dx = -radius; dx <= radius; ++dx)
            for (int dz = -radius; dz <= radius; ++dz)
                if (std::abs(dx) != radius || std::abs(dz) != radius)
                    setLeaf(c, x + dx, ly, z + dz, (uint8_t)BlockId::LEAVES, 1);
    }
    setLeaf(c, x, top + 1, z, (uint8_t)BlockId::LEAVES, 1);
}

void placeBigOak(mcp_chunk_t* c, int x, int y, int z, GenRandom& rng) {
    int h = 6 + (int)rng.nextInt(6);
    for (int i = 0; i < h; ++i) setLog(c, x, y + i, z, (uint8_t)BlockId::LOG, 0);
    int top = y + h;

    for (int ly = top - 2; ly <= top + 1; ++ly) {
        int r = (ly <= top) ? 2 : 1;
        for (int dx = -r; dx <= r; ++dx)
            for (int dz = -r; dz <= r; ++dz)
                if (std::abs(dx) != r || std::abs(dz) != r || rng.nextBoolean())
                    setLeaf(c, x + dx, ly, z + dz, (uint8_t)BlockId::LEAVES, 0);
    }

    int branches = 2 + (int)rng.nextInt(2);
    for (int b = 0; b < branches; ++b) {
        int by = y + h - 2 - (int)rng.nextInt(3);
        int bx = x + ((rng.nextBoolean() ? 1 : -1) * (1 + (int)rng.nextInt(2)));
        int bz = z + ((rng.nextBoolean() ? 1 : -1) * (1 + (int)rng.nextInt(2)));
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx)
                for (int dz = -1; dz <= 1; ++dz)
                    if (std::abs(dx) + std::abs(dy) + std::abs(dz) <= 2)
                        setLeaf(c, bx + dx, by + dy, bz + dz, (uint8_t)BlockId::LEAVES, 0);
    }
}

void placeJungle(mcp_chunk_t* c, int x, int y, int z, GenRandom& rng) {
    int h = 8 + (int)rng.nextInt(6);
    for (int i = 0; i < h; ++i) setLog(c, x, y + i, z, (uint8_t)BlockId::LOG, 3);
    int top = y + h;
    for (int ly = top - 1; ly <= top + 1; ++ly) {
        int r = (ly == top + 1) ? 1 : 2;
        for (int dx = -r; dx <= r; ++dx)
            for (int dz = -r; dz <= r; ++dz)
                if (std::abs(dx) != r || std::abs(dz) != r || rng.nextBoolean())
                    setLeaf(c, x + dx, ly, z + dz, (uint8_t)BlockId::LEAVES, 3);
    }
    for (int dx = -3; dx <= 3; ++dx)
        for (int dz = -3; dz <= 3; ++dz)
            if (dx * dx + dz * dz <= 9)
                setLeaf(c, x + dx, top - 2, z + dz, (uint8_t)BlockId::LEAVES, 3);
}

void placeAcacia(mcp_chunk_t* c, int x, int y, int z, GenRandom& rng) {
    int h = 4 + (int)rng.nextInt(3);
    int dirX = (int)rng.nextInt(3) - 1;
    int dirZ = (int)rng.nextInt(3) - 1;
    if (dirX == 0 && dirZ == 0) dirX = 1;
    int bend = 2 + (int)rng.nextInt(2);

    int cx = x, cz = z;
    for (int i = 0; i < h; ++i) {
        setLog(c, cx, y + i, cz, (uint8_t)BlockId::WOOD_2, 0);
        if (i < bend) { cx += dirX; cz += dirZ; }
    }
    int top = y + h - 1;
    for (int dx = -2; dx <= 2; ++dx)
        for (int dz = -2; dz <= 2; ++dz)
            if (std::abs(dx) + std::abs(dz) <= 3)
                setLeaf(c, cx + dx, top + 1, cz + dz, (uint8_t)BlockId::LEAVES_2, 0);
    for (int dx = -1; dx <= 1; ++dx)
        for (int dz = -1; dz <= 1; ++dz)
            setLeaf(c, cx + dx, top + 2, cz + dz, (uint8_t)BlockId::LEAVES_2, 0);
}

void placeDarkOak(mcp_chunk_t* c, int x, int y, int z, GenRandom& rng) {
    int h = 6 + (int)rng.nextInt(3);
    for (int i = 0; i < h; ++i)
        for (int dx = 0; dx <= 1; ++dx)
            for (int dz = 0; dz <= 1; ++dz)
                setLog(c, x + dx, y + i, z + dz, (uint8_t)BlockId::WOOD_2, 1);

    int top = y + h;
    for (int ly = top - 2; ly <= top + 1; ++ly) {
        int r = (ly <= top) ? 3 : 2;
        for (int dx = -r; dx <= r + 1; ++dx)
            for (int dz = -r; dz <= r + 1; ++dz)
                if (std::abs(dx) != r || std::abs(dz) != r || rng.nextBoolean())
                    setLeaf(c, x + dx, ly, z + dz, (uint8_t)BlockId::LEAVES_2, 1);
    }
}

} // namespace

bool TreeGenerator::place(mcp_chunk_t* c, int x, int y, int z, TreeType t, GenRandom& rng) {
    if (!inXZ(x) || !inXZ(z) || y < 1 || y > kMaxY - 1) return false;
    // Ground below must be soil-ish; otherwise skip (avoids trees on water/stone).
    uint8_t below = mcp_chunk_get_block_id(c, x, y - 1, z);
    bool soil = below == (uint8_t)BlockId::GRASS || below == (uint8_t)BlockId::DIRT ||
                below == (uint8_t)BlockId::SAND || below == (uint8_t)BlockId::CLAY;
    if (!soil) return false;

    switch (t) {
        case TREE_OAK:        placeOak(c, x, y, z, rng); break;
        case TREE_BIG_OAK:    placeBigOak(c, x, y, z, rng); break;
        case TREE_BIRCH:      placeBirch(c, x, y, z, rng); break;
        case TREE_SPRUCE:     placeSpruce(c, x, y, z, rng); break;
        case TREE_JUNGLE:     placeJungle(c, x, y, z, rng); break;
        case TREE_ACACIA:     placeAcacia(c, x, y, z, rng); break;
        case TREE_DARK_OAK:   placeDarkOak(c, x, y, z, rng); break;
        case TREE_SWAMP:      placeSwamp(c, x, y, z, rng); break;
        default:              placeOak(c, x, y, z, rng); break;
    }
    return true;
}

} // namespace cursestone::level::generator::object
