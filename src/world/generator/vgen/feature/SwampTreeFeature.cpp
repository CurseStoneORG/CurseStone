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

#include "vgen/feature/SwampTreeFeature.h"
#include "vgen/Blocks.h"
#include "vgen/Blocks.h"
#include "vgen/material/Material.h"
#include "vgen/Vanilla.h"
#include "vgen/VgenWorld.h"
#include "vgen/Blocks.h"
#include "vgen/VgenWorld.h"

namespace vgen {

SwampTreeFeature::SwampTreeFeature()
	: TreeFeature(nullptr, enum_cast(OldLogBlock::LogType::Oak), LeafBlock::NORMAL_LEAF) {
}

bool SwampTreeFeature::place(BlockSource& region, const BlockPos& pos, Random& random) const {
	BlockPos blockPos(pos);
	int treeHeight = random.nextInt(4) + 5;

	while (region.getMaterial(blockPos.below()).isType(MaterialType::Water) ||
		   region.getBlockID(blockPos.below()) == Block::mLog->mID) {
		blockPos = blockPos.below();
	}

	bool free = true;
	if (blockPos.y < 1 || blockPos.y + treeHeight + 1 > region.getMaxHeight()) {
		return false;
	}

	for (auto yy : range_incl(blockPos.y, blockPos.y + 1 + treeHeight)) {
		int r = (yy == blockPos.y) ? 0 : (yy >= blockPos.y + 1 + treeHeight - 2) ? 3 : 1;

		for (int xx = blockPos.x - r; xx <= blockPos.x + r && free; xx++) {
			for (int zz = blockPos.z - r; zz <= blockPos.z + r && free; zz++) {
				if (yy >= 0 && yy < region.getMaxHeight()) {
					const Block& block = region.getBlock(BlockPos(xx, yy, zz));
					if (!block.isType(Block::mLog) && !_isFree(block.getMaterial())) {
						if (block.getMaterial().isType(MaterialType::Water)) {
							free = (yy > blockPos.y) ? false : free;
						}
						else {
							free = false;
						}
					}
				}
				else {
					free = false;
				}
			}
		}
	}

	if (!free) {
		return false;
	}

	BlockID belowBlock = region.getBlockID(blockPos.below());
	if ((belowBlock != Block::mGrass->mID && belowBlock != Block::mDirt->mID) || blockPos.y >= region.getMaxHeight() - treeHeight - 1) {
		return false;
	}

	_setBlockAndData(region, blockPos.below(), FullBlock(Block::mDirt->mID));

	for (auto yy : range_incl(blockPos.y - 3 + treeHeight, blockPos.y + treeHeight)) {
		int yo = yy - (blockPos.y + treeHeight);
		int offs = 2 - yo / 2;

		for (auto xx : range_incl(blockPos.x - offs, blockPos.x + offs)) {
			int xo = xx - blockPos.x;

			for (auto zz : range_incl(blockPos.z - offs, blockPos.z + offs)) {
				int zo = zz - blockPos.z;
				if (std::abs(xo) == offs && std::abs(zo) == offs && (random.nextInt(2) == 0 || yo == 0)) {
					continue;
				}
				BlockPos leavesPos(xx, yy, zz);
				if (!region.isSolidBlockingBlock(leavesPos)) {
					_setBlockAndData(region, leavesPos, FullBlock(Block::mLeaves->mID));
				}
			}
		}
	}

	for (auto hh : range(treeHeight)) {
		auto& aboveMat = region.getMaterial(blockPos.above(hh));
		if (_isFree(aboveMat) || aboveMat.isType(MaterialType::Water)) {
			_setBlockAndData(region, blockPos.above(hh), FullBlock(Block::mLog->mID));
		}
	}

	for (auto yy : range_incl(blockPos.y - 3 + treeHeight, blockPos.y + treeHeight)) {
		int yo = yy - (blockPos.y + treeHeight);
		int offs = 2 - yo / 2;

		for (auto xx : range_incl(blockPos.x - offs, blockPos.x + offs)) {
			for (auto zz : range_incl(blockPos.z - offs, blockPos.z + offs)) {
				BlockPos vinePos(xx, yy, zz);

				if (region.getMaterial(vinePos).isType(MaterialType::Leaves)) {
					BlockPos west = vinePos.west();
					BlockPos east = vinePos.east();
					BlockPos north = vinePos.north();
					BlockPos south = vinePos.south();

					if (random.nextInt(4) == 0 && region.getMaterial(west).isType(MaterialType::Air)) {
						_addVine(region, west, VineBlock::VINE_EAST);
					}
					if (random.nextInt(4) == 0 && region.getMaterial(east).isType(MaterialType::Air)) {
						_addVine(region, east, VineBlock::VINE_WEST);
					}
					if (random.nextInt(4) == 0 && region.getMaterial(north).isType(MaterialType::Air)) {
						_addVine(region, north, VineBlock::VINE_SOUTH);
					}
					if (random.nextInt(4) == 0 && region.getMaterial(south).isType(MaterialType::Air)) {
						_addVine(region, south, VineBlock::VINE_NORTH);
					}
				}
			}
		}
	}

	return true;
}

void SwampTreeFeature::_addVine(BlockSource& region, BlockPos pos, int dir) const {
	FullBlock vineData(Block::mVine->mID, dir);
	_setBlockAndData(region, pos, vineData);

	int maxDir = 4;
	pos = pos.below();

	while (region.getMaterial(pos).isType(MaterialType::Air) && maxDir > 0) {
		_setBlockAndData(region, pos, vineData);
		pos = pos.below();
		maxDir--;
	}
}

} // namespace vgen

