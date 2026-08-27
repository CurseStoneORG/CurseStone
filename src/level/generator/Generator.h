// Generator — abstract world generator interface.
// Mirrors Nukkit's cn.nukkit.level.generator.Generator: a generator is
// asked to populate a chunk's terrain, biomes and features. Generators
// run on AsyncPool workers (one chunk at a time), so implementations must
// be self-contained and stateless w.r.t. the main thread (the seed is
// the only state).
//
// CRITICAL: each generator defines its OWN spawn point via getSpawn().
// This mirrors Nukkit where Normal returns (127.5, 128, 127.5), Flat
// returns (128, floorLevel, 128), and Nether returns (128, 64, 128).
// The Level no longer hardcodes a spawn position — it asks the generator.
#pragma once
#include "level/format/Chunk.h"
#include "math/Vector3.h"
#include <stdint.h>

namespace atomikpe::level::generator {

struct GenerationContext {
    int64_t seed;          // world seed
    int32_t chunk_x;
    int32_t chunk_z;
    const uint8_t* biomes_in;   // optional input biome map (256 bytes), NULL = generate
    mcp_chunk_t* chunk;        // target chunk — generator fills sections + biomes + heightmap
};

class Generator {
public:
    virtual ~Generator() = default;
    virtual const char* name() const = 0;

    // Phase 1: terrain + biomes + heightmap
    virtual void generate(GenerationContext& ctx) = 0;

    // Phase 2: features (trees, ores, grass, flowers, caves) — may
    // touch neighbouring chunks; called after terrain_populated=true.
    virtual void populate(GenerationContext& ctx) = 0;

    // Returns the world spawn position for this generator type.
    // Mirrors Nukkit's Generator.getSpawn() — each generator type
    // defines its own spawn (Normal, Flat, Nether all differ).
    // The Level calls this on first load to set the spawn point.
    virtual mcp_vector3_t getSpawn() const = 0;
};

} // namespace
