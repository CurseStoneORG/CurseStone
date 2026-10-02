// SPDX-License-Identifier: LGPL-3.0-or-later
// Mountains (Extreme Hills) — biome 4. Rocky peaks with stone outcrops,
// sparse grass and a few high spruce.
#include "BiomeConfig.h"

namespace cursestone::level::generator::biome {
const BiomeConfig& mountainsBiome() {
    static const BiomeConfig c{
        .id = 4,
        .name = "extreme_hills",
        .temp = -0.2f,
        .humid = 0.0f,
        .topBlock = 2,       // grass (stone patches added by the terrain pass)
        .fillerBlock = 1,    // stone
        .snowy = false,      // high peaks get snow by height, not by climate
        .underwater = false,
        .treeAmount = 3,
        .trees = {
            { object::TREE_SPRUCE, 3 },
            { object::TREE_OAK, 1 },
        },
        .grassAmount = 3,
        .flowerAmount = 1,
        .doublePlantAmount = 0,
        .cactusAmount = 0,
        .deadBushAmount = 1,
        .sugarCaneAmount = 0,
        .mushroomAmount = 0,
        .pumpkinAmount = 0,
        .melonAmount = 0,
        .waterLilyAmount = 0,
    };
    return c;
}
} // namespace cursestone::level::generator::biome
