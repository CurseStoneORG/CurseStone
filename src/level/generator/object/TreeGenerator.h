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
    // `x,z` are chunk-local, `y` is the first trunk block (ground + 1).
    // Returns true if a tree was placed. rng drives shape variation.
    static bool place(mcp_chunk_t* c, int x, int y, int z, TreeType t, GenRandom& rng);
};

} // namespace cursestone::level::generator::object
