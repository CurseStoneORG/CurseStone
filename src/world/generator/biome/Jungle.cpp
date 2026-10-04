// SPDX-License-Identifier: LGPL-3.0-or-later
// Jungle — biome 21. Hot and wet: thick canopy, melons, dense grass.
#include "BiomeConfig.h"

namespace cursestone::world::generator::biome {
const BiomeConfig& jungleBiome() {
    static const BiomeConfig c{
        .id = 21,
        .name = "jungle",
        .temp = 0.75f,
        .humid = 0.8f,
        .topBlock = 2,
        .fillerBlock = 3,
        .snowy = false,
        .underwater = false,
        .treeAmount = 9,
        .trees = {
            { object::TREE_JUNGLE, 6 },
            // The big jungle tree: rare, but it is the tree the jungle is
            // actually known for, so it gets a real share of the slots.
            { object::TREE_BIG_JUNGLE, 2 },
            { object::TREE_BIG_OAK, 1 },
            { object::TREE_OAK, 1 },
        },
        .grassAmount = 14,
        .flowerAmount = 2,
        .doublePlantAmount = 4,
        .cactusAmount = 0,
        .deadBushAmount = 0,
        .sugarCaneAmount = 3,
        .mushroomAmount = 1,
        .pumpkinAmount = 0,
        .melonAmount = 4,
        .waterLilyAmount = 0,
    };
    return c;
}
} // namespace cursestone::world::generator::biome
