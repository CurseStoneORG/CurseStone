// SPDX-License-Identifier: LGPL-3.0-or-later
// Taiga — biome 5. Cold conifer forest: spruce trees and ferns.
#include "BiomeConfig.h"

namespace cursestone::world::generator::biome {
const BiomeConfig& taigaBiome() {
    static const BiomeConfig c{
        .id = 5,
        .name = "taiga",
        .temp = -0.35f,
        .humid = 0.2f,
        .topBlock = 2,
        .fillerBlock = 3,
        .snowy = false,
        .underwater = false,
        .treeAmount = 8,
        .trees = {
            { object::TREE_SPRUCE, 4 },
            { object::TREE_OAK, 1 },
        },
        .grassAmount = 4,
        .flowerAmount = 0,
        .doublePlantAmount = 3,
        .cactusAmount = 0,
        .deadBushAmount = 1,
        .sugarCaneAmount = 1,
        .mushroomAmount = 1,
        .pumpkinAmount = 0,
        .melonAmount = 0,
        .waterLilyAmount = 0,
    };
    return c;
}
} // namespace cursestone::world::generator::biome
