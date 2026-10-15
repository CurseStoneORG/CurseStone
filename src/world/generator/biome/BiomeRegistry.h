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
// BiomeRegistry — maps a biome id to its BiomeConfig, and picks the biome
// for a column from smoothed climate + terrain height.
//
// Keeping the selection here (instead of inline in Normal.cpp) is what
// lets every biome live in its own file: Normal only calls select().
#pragma once
#include "BiomeConfig.h"
#include <cstdint>

namespace cursestone::world::generator::biome {

class BiomeRegistry {
  public:
    // Config for a biome id (hill/variant ids alias their base biome;
    // unknown ids fall back to plains so decoration never crashes).
    static const BiomeConfig& forId(uint8_t id);

    // Pick the climate+height biome. `height` is the final surface height
    // and `seaLevel` the world sea level (64).
    static uint8_t select(double temp, double humid, int height, int seaLevel);
};

} // namespace cursestone::world::generator::biome
