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

#include "vgen/feature/GroundBushFeature.h"
#include "vgen/Random.h"
#include "vgen/VgenWorld.h"
#include "vgen/Blocks.h"
#include "vgen/material/Material.h"

namespace vgen {

GroundBushFeature::GroundBushFeature(int trunkType, int leafType) :
	TreeFeature(nullptr, trunkType, leafType) {

}

bool GroundBushFeature::place(BlockSource& region, const BlockPos& pos, Random& random) const {
	BlockPos blockPos(pos);
	while (_isFree(region.getMaterial(blockPos)) && blockPos.y > 0) {
		blockPos = blockPos.below();
	}

	const BlockID block = region.getBlockID(blockPos);
	if (block == Block::mDirt->mID || block == Block::mGrass->mID) {
		_setBlockAndData(region, blockPos, FullBlock(Block::mLog->mID, _getLeafType()));

		for (auto yy : range_incl(blockPos.y, blockPos.y + 2)) {
			int yo = yy - blockPos.y;
			int offs = 2 - yo;

			for (auto xx : range_incl(blockPos.x - offs, blockPos.x + offs)) {
				int xo = xx - (blockPos.x);

				for (auto zz : range_incl(blockPos.z - offs, blockPos.z + offs)) {
					int zo = zz - (blockPos.z);
					if (std::abs(xo) == offs && std::abs(zo) == offs && random.nextInt(2) == 0) {
						continue;
					}

					BlockPos leafBlock(xx, yy, zz);
					const Block* t = Block::mBlocks[region.getBlockID(leafBlock)];
					if (t == nullptr || (t != nullptr && !t->isSolid())) {
						_setBlockAndData(region, leafBlock, FullBlock(Block::mLeaves->mID, _getLeafType()));
					}
				}
			}
		}
	}

	return true;
}

} // namespace vgen

