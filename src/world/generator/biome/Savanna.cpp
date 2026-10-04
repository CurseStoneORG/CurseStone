// SPDX-License-Identifier: LGPL-3.0-or-later
// Savanna — biome 35. Hot and moderately dry: acacia trees, tall grass.
#include "BiomeConfig.h"

namespace cursestone::level::generator::biome {
const BiomeConfig& savannaBiome() {
    static const BiomeConfig c{
        .id = 35,
        .name = "savanna",
        .temp = 0.75f,
        .humid = -0.1f,
        .topBlock = 2,
        .fillerBlock = 3,
        .snowy = false,
        .underwater = false,
        .treeAmount = 3,
        .trees = {
            { object::TREE_ACACIA, 5 },
        },
        .grassAmount = 8,
        .flowerAmount = 2,
        .doublePlantAmount = 1,
        .cactusAmount = 0,
        .deadBushAmount = 1,
        .sugarCaneAmount = 2,
        .mushroomAmount = 0,
        .pumpkinAmount = 0,
        .melonAmount = 0,
        .waterLilyAmount = 0,
    };
    return c;
}
} // namespace cursestone::level::generator::biome
