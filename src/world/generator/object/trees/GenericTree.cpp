// SPDX-License-Identifier: LGPL-3.0-or-later
#include "GenericTree.h"
#include "block/BlockIds.h"
#include <cstdlib>   // std::abs

namespace cursestone::world::generator::object::trees {

using cursestone::block::BlockId;

bool GenericTree::replaceable(uint8_t id) {
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
        case (uint8_t)BlockId::DOUBLE_PLANT:
            return true;
        default:
            return false;
    }
}

void GenericTree::setLeaf(mcp_chunk_t* c, int x, int y, int z, uint8_t id, uint8_t data) {
    if (!inXZ(x) || !inXZ(z) || !inY(y)) return;
    if (replaceable(mcp_chunk_get_block_id(c, x, y, z)))
        mcp_chunk_set_block_full(c, x, y, z, id, data);
}

void GenericTree::setLog(mcp_chunk_t* c, int x, int y, int z, uint8_t id, uint8_t data) {
    if (!inXZ(x) || !inXZ(z) || !inY(y)) return;
    if (replaceable(mcp_chunk_get_block_id(c, x, y, z)))
        mcp_chunk_set_block_full(c, x, y, z, id, data);
}

void GenericTree::setForced(mcp_chunk_t* c, int x, int y, int z, uint8_t id, uint8_t data) {
    if (!inXZ(x) || !inXZ(z) || !inY(y)) return;
    mcp_chunk_set_block_full(c, x, y, z, id, data);
}

void GenericTree::init(GenRandom& rng) {
    // Vanilla oak: 4..6 blocks tall.
    height_ = 4 + (int)rng.nextInt(3);
    logId_ = 17; logData_ = 0;
    leafId_ = 18; leafData_ = 0;
}

bool GenericTree::canPlaceOn(uint8_t soil) const {
    return soil == (uint8_t)BlockId::GRASS || soil == (uint8_t)BlockId::DIRT;
}

void GenericTree::placeCanopy(mcp_chunk_t* c, int sourceX, int sourceY,
                              int sourceZ, GenRandom& rng) {
    // Reference GenericTree::Generate: layers from sourceY+height-3 up to
    // sourceY+height, radius = 1 - n/2 (so 2 at the bottom, 1 at the top),
    // corner blocks randomly culled unless it is the very top layer.
    for (int y = sourceY + height_ - 3; y <= sourceY + height_; ++y) {
        int n = y - (sourceY + height_);
        int radius = 1 - n / 2;   // integer division: -3->2, -2->2, -1->1, 0->1
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

void GenericTree::placeTrunk(mcp_chunk_t* c, int sourceX, int sourceY,
                             int sourceZ, GenRandom& /*rng*/) {
    for (int i = 0; i < height_; ++i)
        setLog(c, sourceX, sourceY + i, sourceZ, logId_, logData_);
}

bool GenericTree::generate(mcp_chunk_t* c, int sourceX, int sourceY,
                           int sourceZ, uint8_t soilId, GenRandom& rng) {
    init(rng);

    if (sourceY < 2 || sourceY > 124) return false;
    if (!canPlaceOn(soilId)) return false;

    placeCanopy(c, sourceX, sourceY, sourceZ, rng);
    placeTrunk(c, sourceX, sourceY, sourceZ, rng);

    // The block right under the trunk always becomes dirt (reference). Only
    // touches the chunk when the trunk actually falls inside it.
    if (inXZ(sourceX) && inXZ(sourceZ)) {
        uint8_t below = mcp_chunk_get_block_id(c, sourceX, sourceY - 1, sourceZ);
        if (below == (uint8_t)BlockId::GRASS || below == (uint8_t)BlockId::DIRT)
            setForced(c, sourceX, sourceY - 1, sourceZ, (uint8_t)BlockId::DIRT, 0);
    }
    return true;
}

} // namespace cursestone::world::generator::object::trees
