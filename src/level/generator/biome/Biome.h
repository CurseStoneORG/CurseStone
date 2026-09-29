// Biome — abstract biome descriptor.
// Biomes in MCPE 0.15.x are encoded as a single byte per column
// (256 bytes per chunk). The biome_definitions.json maps each ID
// to properties (temperature, rainfall, topBlock, fillerBlock).
// Implementations are flyweights: stateless, identified by ID.
#pragma once
#include <cstdint>
#include <string>

namespace cursestone::level::generator::biome {

enum BiomeId : uint8_t {
    BIOME_OCEAN        = 0,
    BIOME_PLAINS        = 1,
    BIOME_DESERT        = 2,
    BIOME_FOREST        = 3,
    BIOME_EXTREME_HILLS = 4,
    BIOME_TAIGA         = 5,
    BIOME_SWAMPLAND     = 6,
    BIOME_RIVER         = 7,
    BIOME_HELL          = 8,   // nether
    BIOME_SKY           = 9,   // end
    BIOME_FROZEN_OCEAN  = 10,
    BIOME_FROZEN_RIVER  = 11,
    BIOME_ICE_PLAINS    = 12,
    BIOME_ICE_MOUNTAINS = 13,
    BIOME_MUSHROOM      = 14,
    BIOME_MUSHROOM_ISLAND = 15,
    BIOME_BEACH         = 16,
    BIOME_DESERT_HILLS  = 17,
    BIOME_FOREST_HILLS  = 18,
    BIOME_TAIGA_HILLS   = 19,
    BIOME_MOUNTAINS     = 20, // extreme hills edge
    BIOME_JUNGLE        = 21,
    BIOME_JUNGLE_HILLS  = 22,
    BIOME_SAVANNA       = 35,
    BIOME_SAVANNA_PLATEAU = 36,
    BIOME_MESA          = 37,
};

class Biome {
public:
    virtual ~Biome() = default;
    virtual uint8_t id() const = 0;
    virtual const char* name() const = 0;
    virtual float temperature() const { return 0.5f; }
    virtual float rainfall() const { return 0.5f; }
    // Top block ID (e.g. grass for plains, sand for desert).
    virtual uint8_t topBlock() const { return 2; } // grass
    // Filler block ID below the top layer (dirt for most biomes).
    virtual uint8_t fillerBlock() const { return 3; } // dirt
    // Surface liquid (water/ocean = 8, lava = 10, none = 0).
    virtual uint8_t liquidBlock() const { return 8; } // water
    virtual bool isFrozen() const { return false; }
    virtual bool isHot() const { return temperature() >= 1.0f; }
};

} // namespace
