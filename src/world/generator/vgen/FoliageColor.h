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
// CurseStone | vgen — foliage/grass color helpers.

#pragma once
#include "vgen/Vanilla.h"

namespace vgen {

class FoliageColor {
public:
	static int getGrassColor(float temp, float rain);
	static int getFoliageColor(float temp, float rain);
	static int getMapGrassColor(float temp, float rain);
	static int getMapFoliageColor(float temp, float rain);
	static int getEvergreenColor(float temp, float rain);
	static int getBirchColor(float temp, float rain);
	static int getMapEvergreenColor(float temp, float rain);
	static int getMapBirchColor(float temp, float rain);
	static int getDefaultColor();
};

} // namespace vgen
