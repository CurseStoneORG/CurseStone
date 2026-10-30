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
// CurseStone | vgen — mob herd helper stubs (dead code: the server

#pragma once
#include "vgen/biome/MobSpawnerData.h"

namespace vgen {

class MobSpawnUtils {
public:
	static void spawnHorseHerd(std::vector<Mob*>& mobHerd, Random& random) {
		UNUSED_VARIABLE(mobHerd);
		UNUSED_VARIABLE(random);
	}
	static void spawnPolarBearHerd(std::vector<Mob*>& mobHerd, Random& random) {
		UNUSED_VARIABLE(mobHerd);
		UNUSED_VARIABLE(random);
	}
	static EntityDefinitionIdentifier spawnZombie(Random& random) {
		UNUSED_VARIABLE(random);
		return EntityDefinitionIdentifier("zombie");
	}
};

} // namespace vgen

