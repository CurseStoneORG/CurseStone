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
// Ocean — biome 0. Deep water, gravel/sand floor, no sky vegetation.
#include "BiomeConfig.h"

namespace cursestone::world::generator::biome {
const BiomeConfig& oceanBiome() {
    static const BiomeConfig c{
        .id = 0,
        .name = "ocean",
        .temp = 0.5f,
        .humid = 0.5f,
        .topBlock = 12,    // sand (floor near shore)
        .fillerBlock = 13, // gravel
        .snowy = false,
        .underwater = true,
        .treeAmount = 0,
        .trees = {},
        .grassAmount = 0,
        .flowerAmount = 0,
        .doublePlantAmount = 0,
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
