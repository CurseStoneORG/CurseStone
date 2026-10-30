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

#include "vgen/feature/VinesFeature.h"
#include "vgen/VgenWorld.h"
#include "vgen/Blocks.h"
#include "vgen/Vanilla.h"
#include "vgen/Facing.h"
#include "vgen/Direction.h"
#include "vgen/Random.h"

namespace vgen {

bool VinesFeature::place(BlockSource& region, const BlockPos& pos, Random& random) const {
	BlockPos blockPos(pos);
	while (blockPos.y < region.getMaxHeight()) {
		if (region.isEmptyBlock(blockPos)) {
			for (FacingID face = Facing::NORTH; face <= Facing::EAST; ++face) {
				if (Block::mVine->mayPlace(region, blockPos, face)) {
					int faceOpposite = 1 << Direction::FACING_DIRECTION[Facing::OPPOSITE_FACING[face]];

					DataID data = 0;
					Block::mVine->getBlockState(BlockState::VineGrowth).set(data, faceOpposite);

					_setBlockAndData(region, blockPos, {Block::mVine->mID, data});
					break;
				}
			}
		}
		else {
			blockPos.x = pos.x + random.nextGaussianInt(4);
			blockPos.z = pos.z + random.nextGaussianInt(4);
		}
		blockPos.y++;
	}

	return true;
}

} // namespace vgen

