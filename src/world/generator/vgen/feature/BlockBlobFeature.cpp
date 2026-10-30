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

#include "vgen/feature/BlockBlobFeature.h"
#include "vgen/VgenWorld.h"
#include "vgen/Random.h"
#include "vgen/Blocks.h"

namespace vgen {

BlockBlobFeature::BlockBlobFeature(BlockID block, int radius) :
	mBlock(block)
	, mStartRadius(radius) {

}

bool BlockBlobFeature::place(BlockSource& region, const BlockPos& pos, Random& random) const {
	BlockPos blockPos(pos);
	while (blockPos.y > 3) {
		if (!region.isEmptyBlock(blockPos.below())) {
			BlockID subBlock = region.getBlockID(blockPos.below());
			if (subBlock == Block::mGrass->mID || subBlock == Block::mDirt->mID || subBlock == Block::mStone->mID) {
				break;
			}
		}
		blockPos = blockPos.below();
	}

	if (blockPos.y <= 3) {
		return false;
	}

	int c = 0;

	while (mStartRadius >= 0 && c < 3) {
		int xr = mStartRadius + random.nextInt(2);
		int yr = mStartRadius + random.nextInt(2);
		int zr = mStartRadius + random.nextInt(2);
		float tr = (xr + yr + zr) * .333f + .5f;

		for (auto x : range_incl(blockPos.x - xr, blockPos.x + xr)) {
			for (auto y : range_incl(blockPos.y - yr, blockPos.y + yr)) {
				for (auto z : range_incl(blockPos.z - zr, blockPos.z + zr)) {
					BlockPos blobPos(x, y, z);
					if (blobPos.distSqr(blockPos) <= tr * tr) {
						_setBlockAndData( region, blobPos, mBlock);
					}
				}
			}
		}

		int offsetX = -(mStartRadius + 1) + random.nextInt(2 + mStartRadius * 2);
		int offsetY = 0 - random.nextInt(2);
		int offsetZ = -(mStartRadius + 1) + random.nextInt(2 + mStartRadius * 2);
		blockPos = blockPos.offset(offsetX, offsetY, offsetZ);
		c++;
	}
	return true;
}

} // namespace vgen

