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

#pragma once

#include "vgen/biome/Biome.h"

namespace vgen {

class OceanBiome : public Biome {
public:

	OceanBiome(int id) :
		Biome(id, Biome::BiomeType::Ocean) {
		_friendlies.clear();

		_waterFriendlies.insert(_waterFriendlies.end(), MobSpawnerData(EntityType::Squid, 10, 1, 2));
	}

	BiomeTempCategory getTemperatureCategory() const override {
		return Biome::BiomeTempCategory::OCEAN;
	}

	virtual float getCreatureProbability() override {
		return 0.1f;
	}

};

} // namespace vgen

