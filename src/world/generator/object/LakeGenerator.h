// SPDX-License-Identifier: LGPL-3.0-or-later
// LakeGenerator — small above-ground water/lava pools.
// Adapted from NetherGamesMC/ext-vanillagenerator lib/generator/object/Lake.
#pragma once
#include "world/format/Chunk.h"
#include "world/generator/noise/GenRandom.h"
#include <cstdint>

namespace cursestone::level::generator::object {

class LakeGenerator {
public:
    // Carve an ellipsoidal pool centered on (x,y,z) (chunk-local).
    // liquidId is WATER (9) or LAVA (11). Returns true if one was carved.
    static bool place(mcp_chunk_t* c, int x, int y, int z,
                      uint8_t liquidId, GenRandom& rng);
};

} // namespace cursestone::level::generator::object
