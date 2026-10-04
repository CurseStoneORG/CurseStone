// SPDX-License-Identifier: LGPL-3.0-or-later
// BiomeConfig — everything the generator needs to know about one biome:
// its climate band, its surface blocks and how much decoration it grows.
//
// Each biome lives in its own file under biome/ (Plains.cpp, Desert.cpp,
// ...) and registers itself with BiomeRegistry. Normal.cpp and the
// populators only ever read BiomeConfig, never biome-specific code.
#pragma once
#include "world/generator/object/TreeType.h"
#include <cstdint>
#include <vector>

namespace cursestone::world::generator::biome {

// A tree species with a relative weight inside one biome.
struct TreeSpawn {
    object::TreeType type;
    int weight;
};

struct BiomeConfig {
    uint8_t id = 0;
    const char* name = "";

    // Climate centre in [-1, 1] (used for documentation / diagnostics).
    float temp = 0.0f;
    float humid = 0.0f;

    // Surface blocks (legacy numeric ids, MCPE 0.15.x).
    uint8_t topBlock = 2;    // grass
    uint8_t fillerBlock = 3; // dirt
    bool snowy = false;      // cold: snow layer / ice / snow block
    bool underwater = false; // lives below sea level (no sky vegetation)

    // Decoration amounts per chunk.
    int treeAmount = 0;
    std::vector<TreeSpawn> trees;

    int grassAmount = 0;
    int flowerAmount = 0;
    int doublePlantAmount = 0;
    int cactusAmount = 0;
    int deadBushAmount = 0;
    int sugarCaneAmount = 0;
    int mushroomAmount = 0;
    int pumpkinAmount = 0;
    int melonAmount = 0;
    int waterLilyAmount = 0;
};

} // namespace cursestone::world::generator::biome
