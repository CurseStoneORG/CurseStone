// SPDX-License-Identifier: LGPL-3.0-or-later
// OreGenerator — places ore veins inside a chunk.
//
// Vein shape and the per-ore distribution are ported from
// NetherGamesMC/ext-vanillagenerator (object/OreVein + populators/
// OrePopulator): a vein is a chain of ellipsoids along a short random
// line, replacing only the target block (stone by default).
#pragma once
#include "world/format/Chunk.h"
#include <cstdint>

namespace cursestone::world::generator::object {

struct OreVein {
    uint8_t block_id;       // ore block id (e.g. 16 = coal ore)
    uint8_t target_id;      // block it replaces (1 = stone)
    int     min_y;          // lowest Y a vein center can spawn
    int     max_y;          // highest Y
    int     amount;         // ~blocks per vein (ellipsoid size)
    int     total;          // vein attempts per chunk
};

class OreGenerator {
public:
    // Places veins based on a deterministic per-chunk random derived from
    // `seed`, the vein block id and the chunk coordinates. Returns the
    // number of ore blocks placed.
    static int place(mcp_chunk_t* c, int64_t seed, const OreVein& v);
};

} // namespace cursestone::world::generator::object
