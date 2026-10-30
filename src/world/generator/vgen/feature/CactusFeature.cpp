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

#include "vgen/feature/CactusFeature.h"
#include "vgen/VgenWorld.h"

namespace vgen {

bool CactusFeature::place(BlockSource& region, const BlockPos& pos, Random& random) const {
	for (int i = 0; i < 10; i++) {

		const int randZ = random.nextGaussianInt(8);
		const int randY = random.nextGaussianInt(4);
		const int randX = random.nextGaussianInt(8);
		BlockPos blockPos = pos.offset(randX, randY, randZ);

		if (region.isEmptyBlock(blockPos)) {
			int randh = random.nextInt(3) + 1;
			int h = 1 + random.nextInt(randh);

			for (int yy = 0; yy < h; yy++) {
				if (Block::mCactus->canSurvive(region, blockPos.above(yy))) {
					_setBlockAndData( region, {blockPos.x, blockPos.y + yy, blockPos.z}, Block::mCactus->mID);
				}
			}
		}
	}

	return true;
}

} // namespace vgen

