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

class ExtremeHillsBiome : public Biome {
	static const bool GENERATE_EMERALD_ORE;
public:

	ExtremeHillsBiome(int id, bool extraTrees);
	const FeaturePtr& getTreeFeature(Random* random) override;
	void decorate(BlockSource* source, Random& random, const BlockPos& origin, bool legacy, float limit) override;
	void buildSurfaceAt(Random& random, BlockVolume& blocks, const BlockPos& pos, float depthValue, Height seaLevel) override;

protected:

	ExtremeHillsBiome* setMutated(Biome* parent);

private:

	static const int TYPE_NORMAL = 0;
	static const int TYPE_EXTRA_TREES = 1;
	static const int TYPE_MUTATED = 2;
	int type;
	FeaturePtr silverFishFeature;
};

} // namespace vgen

