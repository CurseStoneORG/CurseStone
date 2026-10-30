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

#include "vgen/feature/MelonFeature.h"
#include "vgen/Random.h"
#include "vgen/VgenWorld.h"
#include "vgen/Blocks.h"

namespace vgen {

bool MelonFeature::place(BlockSource& region, const BlockPos& pos, Random& random) const {
	for (auto i : range(64)) {
		UNUSED_VARIABLE(i);
		const int randZ = random.nextGaussianInt(8);
		const int randY = random.nextGaussianInt(4);
		const int randX = random.nextGaussianInt(8);
		BlockPos melonPos = pos.offset(randX, randY, randZ);

 		if (Block::mMelon->mayPlace(region, melonPos) && region.getBlockID(melonPos.below()) == Block::mGrass->mID) {
			_setBlockAndData( region, melonPos, Block::mMelon->mID);
		}
	}

	return true;
}

} // namespace vgen

