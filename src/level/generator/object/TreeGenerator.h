// SPDX-License-Identifier: LGPL-3.0-or-later
// TreeGenerator — places a tree at chunk-local coordinates.
//
// Species and shapes are ported from NetherGamesMC/ext-vanillagenerator
// (lib/generator/object/trees). Each species writes the correct legacy
// block id + data value for MCPE 0.15.x (protocol 84):
//   LOG (17)   data 0=oak 1=spruce 2=birch 3=jungle
//   LEAVES(18) data 0=oak 1=spruce 2=birch 3=jungle
//   WOOD_2 (162) data 0=acacia 1=dark oak
//   LEAVES_2(161) data 0=acacia 1=dark oak
//
// Trees that overflow the 16x16 chunk are clipped (we generate one chunk
// at a time and do not touch neighbours), matching the existing behaviour.
#pragma once
#include "level/format/Chunk.h"
#include "level/generator/noise/GenRandom.h"
#include <cstdint>

namespace cursestone::level::generator::object {

enum TreeType {
    TREE_OAK = 0,
    TREE_BIG_OAK = 1,
    TREE_BIRCH = 2,
    TREE_SPRUCE = 3,
    TREE_JUNGLE = 4,
    TREE_ACACIA = 5,
    TREE_DARK_OAK = 6,
    TREE_SWAMP = 7,
};

class TreeGenerator {
public:
    // `x,z` are chunk-local, `y` is the first trunk block (ground + 1).
    // Returns true if a tree was placed. rng drives shape variation.
    static bool place(mcp_chunk_t* c, int x, int y, int z, TreeType t, GenRandom& rng);
};

} // namespace cursestone::level::generator::object
