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
        .trees =
            {
                {object::TREE_SPRUCE, 4},
                {object::TREE_OAK, 1},
            },
        // The taiga floor is fern country: vanilla covers it densely with
        // ferns (and large ferns), not with the sparse grass the old amount
        // grew. The tall-grass pass places data 2 (fern) here, so this
        // amount is the fern density.
        .grassAmount = 10,
        .flowerAmount = 0,
        .doublePlantAmount = 3,
        .cactusAmount = 0,
        .deadBushAmount = 1,
        .sugarCaneAmount = 1,
        .mushroomAmount = 1,
        .melonAmount = 0,
        .waterLilyAmount = 0,
    };
    return c;
}
} // namespace cursestone::world::generator::biome
