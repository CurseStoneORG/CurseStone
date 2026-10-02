// SPDX-License-Identifier: LGPL-3.0-or-later
// TreeGenerator — thin dispatcher: picks the species class for a TreeType
// and generates it. Each species lives in object/trees/<Type>Tree.{h,cpp}.
#pragma once
#include "level/format/Chunk.h"
#include "level/generator/noise/GenRandom.h"
#include "level/generator/object/TreeType.h"
#include <cstdint>

namespace cursestone::level::generator::object {

class TreeGenerator {
public:
    // `x,z` are coordinates relative to the target chunk and MAY be outside
    // 0..15: a tree rooted in a neighbouring chunk writes only the blocks
    // that overlap this chunk (they are clipped), which is what lets trees
    // span chunk borders. `y` is the first trunk block (ground + 1) and
    // `soilId` the block directly below it. Returns true if a tree was
    // placed.
    static bool place(mcp_chunk_t* c, int x, int y, int z, TreeType t,
                      uint8_t soilId, GenRandom& rng);
};

} // namespace cursestone::level::generator::object
