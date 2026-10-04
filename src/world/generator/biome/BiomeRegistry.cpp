// SPDX-License-Identifier: LGPL-3.0-or-later
#include "BiomeRegistry.h"
#include "BiomeConfigs.h"
#include "world/generator/biome/Biome.h"

namespace cursestone::world::generator::biome {

using namespace cursestone::world::generator::biome; // (Biome.h ids)

const BiomeConfig& BiomeRegistry::forId(uint8_t id) {
    switch (id) {
        case BIOME_OCEAN:            return oceanBiome();
        // Rivers share the ocean's surface (sand/gravel bed, no trees).
        case BIOME_RIVER:
        case BIOME_FROZEN_RIVER:     return oceanBiome();
        case BIOME_BEACH:            return beachBiome();
        case BIOME_PLAINS:           return plainsBiome();
        case BIOME_PLAINS_PLATEAU:   return plainsPlateauBiome();
        case BIOME_FOREST:
        case BIOME_FOREST_HILLS:     return forestBiome();
        case BIOME_DESERT:
        case BIOME_DESERT_HILLS:     return desertBiome();
        case BIOME_TAIGA:
        case BIOME_TAIGA_HILLS:      return taigaBiome();
        case BIOME_SAVANNA:
        case BIOME_SAVANNA_PLATEAU:  return savannaBiome();
        case BIOME_JUNGLE:
        case BIOME_JUNGLE_HILLS:     return jungleBiome();
        case BIOME_SWAMPLAND:        return swampBiome();
        case BIOME_ICE_PLAINS:
        case BIOME_ICE_MOUNTAINS:    return icePlainsBiome();
        case BIOME_EXTREME_HILLS:
        case BIOME_MOUNTAINS:        return mountainsBiome();
        default:                     return plainsBiome();
    }
}

uint8_t BiomeRegistry::select(double temp, double humid, int height, int seaLevel) {
    // Below sea level minus a shallow margin: open ocean.
    if (height < seaLevel - 2) return BIOME_OCEAN;

    // High peaks win over climate (mountains are about relief, not heat).
    if (height >= seaLevel + 26) return BIOME_EXTREME_HILLS;

    // Climate bands. Each band is deliberately wide so a single biome
    // spans hundreds of blocks instead of flickering every few metres.
    if (temp < -0.55) return BIOME_ICE_PLAINS;
    if (temp < -0.25) return BIOME_TAIGA;

    if (temp > 0.5) {
        // Hot: very dry -> desert, wet -> jungle, in between -> savanna.
        if (humid < -0.20) return BIOME_DESERT;
        if (humid > 0.35)  return BIOME_JUNGLE;
        return BIOME_SAVANNA;
    }
    if (temp > 0.30) {
        return (humid > 0.35) ? BIOME_JUNGLE : BIOME_SAVANNA;
    }

    // Temperate.
    if (humid > 0.65 && height < seaLevel + 6) return BIOME_SWAMPLAND;
    if (humid > 0.15) return BIOME_FOREST;
    return BIOME_PLAINS;
}

} // namespace cursestone::world::generator::biome
