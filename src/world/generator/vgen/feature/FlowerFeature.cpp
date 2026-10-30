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

#include "vgen/feature/FlowerFeature.h"
#include "vgen/Random.h"
#include "vgen/VgenWorld.h"

namespace vgen {

FlowerFeature::FlowerFeature(BlockID block) :
	mBlock(block) {
}

bool FlowerFeature::place(BlockSource& region, const BlockPos& pos, Random& random) const {
	//only used by mushrooms
	return placeFlower(region, pos, mBlock, random);
}

bool FlowerFeature::placeFlower(BlockSource& region, const BlockPos& pos, FullBlock flower, Random& random) const {
	for (auto i : range(64)) {
		UNUSED_VARIABLE(i);
		const int randZ = random.nextGaussianInt(8);
		const int randY = random.nextGaussianInt(4);
		const int randX = random.nextGaussianInt(8);
		BlockPos blockPos = pos.offset(randX, randY, randZ);
		if (region.isEmptyBlock(blockPos)) {
			if (((const BushBlock*)Block::mBlocks[flower.id])->canSurvive(region, blockPos)) {
				_setBlockAndData( region, blockPos, flower);
			}
		}
	}

	return true;
}

} // namespace vgen

