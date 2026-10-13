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
#include "Biome.h"
#include "BiomePlains.h"
#include "BiomeDesert.h"
#include "BiomeForest.h"
#include "BiomeMountains.h"
#include "BiomeOcean.h"
namespace cursestone::world::generator::biome {
uint8_t BiomePlains::id() const    { return BIOME_PLAINS; }
uint8_t BiomeDesert::id() const    { return BIOME_DESERT; }
uint8_t BiomeForest::id() const    { return BIOME_FOREST; }
uint8_t BiomeMountains::id() const { return BIOME_EXTREME_HILLS; }
uint8_t BiomeOcean::id() const     { return BIOME_OCEAN; }
} // namespace
