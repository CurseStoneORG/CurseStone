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

#include "vgen/feature/TallGrassFeature.h"
#include "vgen/Blocks.h"
#include "vgen/Blocks.h"
#include "vgen/VgenWorld.h"

namespace vgen {

TallGrassFeature::TallGrassFeature(BlockID block, DataID type)
	: mGrass(block, type) {
}

bool TallGrassFeature::place(BlockSource& region, const BlockPos& pos, Random& random, int tries, int r) const {
	BlockPos blockPos(pos);
	BlockID t = BlockID::AIR;

	while (((t = region.getBlockID(blockPos)) == 0 || t.hasProperty(BlockProperty::Leaf)) && blockPos.y > 0) {
		blockPos.y--;
	}

	for (int i = 0; i < tries; i++) {
		const int randZ = random.nextGaussianInt(r);
		const int randY = random.nextGaussianInt(4);
		const int randX = random.nextGaussianInt(r);
		BlockPos grassPos = pos.offset(randX, randY, randZ);

		if (region.getBlockID(grassPos) == BlockID::AIR && Block::mBlocks[mGrass.id]->canSurvive(region, grassPos)) {
			_setBlockAndData(region, grassPos, mGrass);
		}
	}

	return true;
}

bool TallGrassFeature::place(BlockSource& region, const BlockPos& pos, Random& random) const {
	return place(region, pos, random, 90, 8);
}

} // namespace vgen

