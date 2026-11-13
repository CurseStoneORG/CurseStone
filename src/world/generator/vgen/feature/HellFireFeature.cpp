/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

 * CurseStone | Open Source Server Software
 *
 * Copyright (C) 2026 CurseStoneORG
 *
 * Developed by CurseStoneORG.
 * Built for an open and extensible Minecraft server ecosystem.
 *
 * Licensed under the GNU Lesser General Public License v3.0 or later.
 * You may study, modify, and redistribute this software under its license.
 *
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 * Repository: https://github.com/CurseStoneORG/CurseStone
 *
 */
#include "vgen/feature/HellFireFeature.h"

#include "vgen/VgenWorld.h"
#include "vgen/Blocks.h"
#include "vgen/Random.h"

namespace vgen {

bool HellFireFeature::place(BlockSource& region, const BlockPos& pos, Random& random) const {
	for (int i = 0; i < 64; i++) {
		const int randZ = random.nextGaussianInt(8);
		const int randY = random.nextGaussianInt(4);
		const int randX = random.nextGaussianInt(8);
		BlockPos firePos = pos.offset(randX, randY, randZ);

		if (!region.isEmptyBlock(firePos)) {
			continue;
		}
		if (region.getBlockID(firePos.below()) != Block::mNetherrack->mID) {
			continue;
		}
		_setBlockAndData(region, firePos, FullBlock(Block::mFire->mID));
	}

	return true;
}

} // namespace vgen
