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

#include "vgen/feature/PumpkinFeature.h"
#include "vgen/Random.h"
#include "vgen/VgenWorld.h"
#include "vgen/Blocks.h"

namespace vgen {

bool PumpkinFeature::place(BlockSource& region, const BlockPos& pos, Random& random) const {
	for (auto i : range(64)) {
		UNUSED_VARIABLE(i);
		const int randX = random.nextGaussianInt(8);
		const int randZ = random.nextGaussianInt(8);
		const int randY = random.nextGaussianInt(4);
		BlockPos pumpkinPos = pos.offset(randX, randY, randZ);
		if (region.isEmptyBlock(pumpkinPos) && region.getBlockID(pumpkinPos.below()) == Block::mGrass->mID) {
			if (Block::mPumpkin->mayPlace(region, pumpkinPos)) {
				_setBlockAndData( region, pumpkinPos, {Block::mPumpkin->mID, (DataID)random.nextInt(4)});
			}
		}
	}

	return true;
}

} // namespace vgen

