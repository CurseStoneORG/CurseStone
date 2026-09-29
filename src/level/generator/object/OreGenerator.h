// OreGenerator — places a single ore vein inside a chunk.
// Veins are small ellipsoids of a given block ID. Used as a
// populator sub-task by Normal generator.
#pragma once
#include "level/format/Chunk.h"
#include <cstdint>

namespace cursestone::level::generator::object {
struct OreVein {
    uint8_t block_id;     // e.g. 14=GoldOre, 15=IronOre, 56=DiamondOre
    uint8_t min_y;         // lowest Y this ore can spawn
    uint8_t max_y;         // highest Y
    uint8_t vein_size;     // ~max blocks per vein
    int rarity;            // 1 in N chunk columns per attempt
};
class OreGenerator {
public:
    // Places ore in chunk based on a seeded random; returns the
    // number of blocks placed.
    static int place(mcp_chunk_t* c, int64_t seed, const OreVein& v);
};
} // namespace
