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

class ForestBiome : public Biome {

public:

	enum {
		TYPE_NORMAL = 0,
		TYPE_FLOWER = 1,
		TYPE_BIRCH = 2,
		TYPE_ROOFED = 3,
	};

	ForestBiome(int id, int forestType);
	Biome& setColor(int color) override;
	Biome& setColor(int color, bool oddColor) override;
	const FeaturePtr& getTreeFeature(Random* random) override;
	FullBlock getRandomFlowerTypeAndData(Random& random, const BlockPos& pos) override;
	void decorate(BlockSource* source, Random& random, const BlockPos& origin, bool legacy, float limit) override;
	bool isHumid() override;

protected:

protected:

	int type;
};

} // namespace vgen

