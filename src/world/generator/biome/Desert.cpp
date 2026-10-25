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
// Desert — biome 2. Hot and dry: sand, cactus, dead bushes, no grass.
#include "BiomeConfig.h"

namespace cursestone::world::generator::biome {
const BiomeConfig& desertBiome() {
    static const BiomeConfig c{
        .id = 2,
        .name = "desert",
        .temp = 0.85f,
        .humid = -0.7f,
        .topBlock = 12,    // sand
        .fillerBlock = 24, // sandstone
        .snowy = false,
        .underwater = false,
        .treeAmount = 0,
        .trees = {},
        .grassAmount = 0,
        .flowerAmount = 0,
        .doublePlantAmount = 0,
        .cactusAmount = 4,
        .deadBushAmount = 3,
        .sugarCaneAmount = 6, // oases along rare water pockets
        .mushroomAmount = 0,
        .melonAmount = 0,
        .waterLilyAmount = 0,
    };
    return c;
}
} // namespace cursestone::world::generator::biome
