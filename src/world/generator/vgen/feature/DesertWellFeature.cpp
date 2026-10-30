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

#include "vgen/feature/DesertWellFeature.h"
#include "vgen/VgenWorld.h"

namespace vgen {

bool DesertWellFeature::place(BlockSource& region, const BlockPos& pos, Random& random) const {
	BlockPos blockPos(pos);
	while (region.isEmptyBlock(blockPos) && blockPos.y > 2) {
		blockPos = blockPos.below();
	}

	if (region.getBlockID(blockPos) != Block::mSand->mID) {
		return false;
	}

	// the surrounding 5x5 area may not be lower than y-1
	for (auto ox : range_incl(-2, 2)) {
		for (auto oz : range_incl(-2, 2)) {
			if (region.isEmptyBlock(blockPos.offset(ox, -1, oz)) && region.isEmptyBlock(blockPos.offset(ox, -2, oz))) {
				return false;
			}
		}
	}

	FullBlock sandStone(Block::mSandStone->mID);
	FullBlock water(Block::mStillWater->mID);
	FullBlock sandSlab(Block::mStoneSlab->mID, enum_cast(StoneSlabBlock::StoneSlabType::Sandstone));

	// place floor
	for (auto oy : range_incl(-1, 0)) {
		for (auto ox : range_incl(-2, 2)) {
			for (auto oz : range_incl(-2, 2)) {
				_setBlockAndData( region, blockPos.offset(ox, oy, oz), sandStone);
			}
		}
	}

	// place water cross
	_setBlockAndData( region, blockPos, water);
	_setBlockAndData( region, blockPos.west(), water);
	_setBlockAndData( region, blockPos.east(), water);
	_setBlockAndData( region, blockPos.north(), water);
	_setBlockAndData( region, blockPos.south(), water);

	// place "fence"
	for (auto ox : range_incl(-2, 2)) {
		for (auto oz : range_incl(-2, 2)) {
			if (ox == -2 || ox == 2 || oz == -2 || oz == 2) {
				_setBlockAndData( region, blockPos.offset(ox, 1, oz), sandStone);
			}
		}
	}
	_setBlockAndData( region, blockPos.offset(2, 1, 0), sandSlab);
	_setBlockAndData( region, blockPos.offset(-2, 1, 0), sandSlab);
	_setBlockAndData( region, blockPos.offset(0, 1, 2), sandSlab);
	_setBlockAndData( region, blockPos.offset(0, 1, -2), sandSlab);

	// place roof
	for (auto ox : range_incl(-1, 1)) {
		for (auto oz : range_incl(-1, 1)) {
			if (ox == 0 && oz == 0) {
				_setBlockAndData( region, blockPos.offset(ox, 4, oz), sandStone);
			}
			else {
				_setBlockAndData( region, blockPos.offset(ox, 4, oz), sandSlab);
			}
		}
	}

	// place pillars
	for (auto oy : range_incl(1, 3)) {
		_setBlockAndData( region, blockPos.offset(-1, oy, -1), sandStone);
		_setBlockAndData( region, blockPos.offset(-1, oy, 1), sandStone);
		_setBlockAndData( region, blockPos.offset(1, oy, -1), sandStone);
		_setBlockAndData( region, blockPos.offset(1, oy, 1), sandStone);
	}

	return true;
}

} // namespace vgen

