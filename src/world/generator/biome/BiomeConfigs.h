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
// BiomeConfigs — accessors for every biome configuration file.
// Each biome lives in its own .cpp under biome/.
#pragma once
#include "BiomeConfig.h"

namespace cursestone::world::generator::biome {

const BiomeConfig& oceanBiome();
const BiomeConfig& beachBiome();
const BiomeConfig& plainsBiome();
const BiomeConfig& plainsPlateauBiome();
const BiomeConfig& forestBiome();
const BiomeConfig& desertBiome();
const BiomeConfig& taigaBiome();
const BiomeConfig& savannaBiome();
const BiomeConfig& jungleBiome();
const BiomeConfig& swampBiome();
const BiomeConfig& icePlainsBiome();
const BiomeConfig& mountainsBiome();

} // namespace cursestone::world::generator::biome
