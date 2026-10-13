/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

 * CurseStone | Open Source Server Software
 *
 * Copyright (C) 2026 CurseStoneTeam
 *
 * Developed by CurseStoneTeam.
 * Built for an open and extensible Minecraft server ecosystem.
 *
 * Licensed under the GNU Lesser General Public License v3.0 or later.
 * You may study, modify, and redistribute this software under its license.
 *
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 * Repository: https://github.com/CurseStoneTeam/CurseStone
 *
 */
// SPDX-License-Identifier: LGPL-3.0-or-later
// Plains Plateau — biome 129. The plains variant: the same open grassland,
// but standing a few blocks higher than the ordinary plain and flat on top
// (the height itself is carved by TerrainShape::plainsPlateau). A few more
// flowers than the base plains, a few birch among the oaks, and just enough
// trees for the grove pass to make sparse copses.
#include "BiomeConfig.h"

namespace cursestone::world::generator::biome {
const BiomeConfig& plainsPlateauBiome() {
    static const BiomeConfig c{
        .id = 129,
        .name = "plains_plateau",
        .temp = 0.1f,
        .humid = -0.2f,
        .topBlock = 2,       // grass
        .fillerBlock = 3,    // dirt
        .snowy = false,
        .underwater = false,
        .treeAmount = 3,
        .trees = {
            { object::TREE_OAK, 6 },
            { object::TREE_BIRCH, 2 },
            { object::TREE_BIG_OAK, 1 },
        },
        .grassAmount = 12,
        .flowerAmount = 12,
        .doublePlantAmount = 3,
        .cactusAmount = 0,
        .deadBushAmount = 0,
        .sugarCaneAmount = 2,
        .mushroomAmount = 0,
        .pumpkinAmount = 1,
        .melonAmount = 0,
        .waterLilyAmount = 0,
    };
    return c;
}
} // namespace cursestone::world::generator::biome
