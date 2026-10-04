// SPDX-License-Identifier: LGPL-3.0-or-later
// Swamp — biome 6. Low and wet: swamp oaks, lily pads, mushrooms.
#include "BiomeConfig.h"

namespace cursestone::world::generator::biome {
const BiomeConfig& swampBiome() {
    static const BiomeConfig c{
        .id = 6,
        .name = "swampland",
        .temp = 0.15f,
        .humid = 0.9f,
        .topBlock = 2,
        .fillerBlock = 3,
        .snowy = false,
        .underwater = false,
        .treeAmount = 5,
        .trees = {
            { object::TREE_SWAMP, 5 },
        },
        .grassAmount = 6,
        .flowerAmount = 2,
        .doublePlantAmount = 1,
        .cactusAmount = 0,
        .deadBushAmount = 0,
        .sugarCaneAmount = 3,
        .mushroomAmount = 1,
        .pumpkinAmount = 0,
        .melonAmount = 0,
        .waterLilyAmount = 8,
    };
    return c;
}
} // namespace cursestone::world::generator::biome
