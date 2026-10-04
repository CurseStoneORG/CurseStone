// SPDX-License-Identifier: LGPL-3.0-or-later
// Ice Plains — biome 12. Frozen tundra: snow cover, a few spruce.
#include "BiomeConfig.h"

namespace cursestone::world::generator::biome {
const BiomeConfig& icePlainsBiome() {
    static const BiomeConfig c{
        .id = 12,
        .name = "ice_plains",
        .temp = -0.9f,
        .humid = 0.1f,
        .topBlock = 2,       // grass (covered by snow layer by the terrain)
        .fillerBlock = 3,
        .snowy = true,
        .underwater = false,
        .treeAmount = 1,
        .trees = {
            { object::TREE_SPRUCE, 1 },
        },
        .grassAmount = 2,
        .flowerAmount = 0,
        .doublePlantAmount = 1,
        .cactusAmount = 0,
        .deadBushAmount = 0,
        .sugarCaneAmount = 0,
        .mushroomAmount = 0,
        .pumpkinAmount = 0,
        .melonAmount = 0,
        .waterLilyAmount = 0,
    };
    return c;
}
} // namespace cursestone::world::generator::biome
