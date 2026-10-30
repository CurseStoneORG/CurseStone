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

#include "vgen/feature/MegaTreeFeature.h"
#include "vgen/Blocks.h"
#include "vgen/Blocks.h"
#include "vgen/VgenWorld.h"
#include "vgen/VgenWorld.h"

namespace vgen {

MegaTreeFeature::MegaTreeFeature(Entity* placer, int baseHeight, int heightInterval, int trunkType, int leafType) :
	TreeFeature(placer, enum_cast(OldLogBlock::LogType::Birch), enum_cast(OldLeafBlock::LeafType::Birch))
	, mBaseHeight(baseHeight)
	, mHeightInterval(heightInterval)
	, mTrunkType(trunkType)
	, mLeafType(leafType) {

}

bool MegaTreeFeature::place(BlockSource& region, const BlockPos& pos, Random& random) const {
	int treeHeight = random.nextInt(3) + 5;
	return TreeFeature::place(region, pos, random, treeHeight);
}

int MegaTreeFeature::_calcTreeHeigth(Random& random) const {
	int height = random.nextInt(3) + mBaseHeight;
	if (mHeightInterval > 1) {
		height += random.nextInt(mHeightInterval);
	}
	return height;
}

bool MegaTreeFeature::_checkIsFree(BlockSource& region, const BlockPos& pos, int treeHeight) const {
	bool free = true;

	if (pos.y < 1 || pos.y + treeHeight + 1 > region.getMaxHeight()) {
		return false;
	}

	for (auto dy : range_incl(1 + treeHeight)) {
		int r = dy == 0 ? 1 : 2;
		for (int dx = -r; dx <= r && free; dx++) {
			for (int dz = -r; dz <= r && free; dz++) {
				if (pos.y + dy >= 0 && pos.y + dy < region.getMaxHeight()) {
					BlockPos offsetPos = pos.offset(dx, dy, dz);
					if (_isFree(region.getBlockID(offsetPos))) {
						continue;
					}
				}
				free = false;
				break;
			}
		}
	}

	return free;
}

bool MegaTreeFeature::_makeDirtFloor(BlockSource& region, const BlockPos& pos) const {
	const BlockPos below = pos.below();
	const BlockID belowBlock = region.getBlockID(below);
	if (!(belowBlock == Block::mGrass->mID || belowBlock == Block::mDirt->mID || belowBlock == Block::mPodzol->mID) || pos.y < 2) {
		return false;
	}

	_setBlockAndData( region, below, Block::mDirt->mID);
	_setBlockAndData( region, below.east(), Block::mDirt->mID);
	_setBlockAndData( region, below.south(), Block::mDirt->mID);
	_setBlockAndData( region, below.south().east(), Block::mDirt->mID);

	return true;
}

bool MegaTreeFeature::_prepareTree(BlockSource& region, const BlockPos& pos, int treeHeight) const {
	return _checkIsFree(region, pos, treeHeight) && _makeDirtFloor(region, pos);
}

void MegaTreeFeature::_placeDoubleTrunkLeaves(BlockSource& region, const BlockPos& origin, int radius) const {
	const int r2 = radius * radius;

	for (auto dx : range_incl(-radius, radius + 1)) {
		for (auto dz : range_incl(-radius, radius + 1)) {
			int odx = dx - 1;
			int ody = dz - 1;
			if (dx * dx + dz * dz > r2 && odx * odx + ody * ody > r2 && dx * dx + ody * ody > r2 && odx * odx + dz * dz > r2) {
				continue;
			}

			const BlockPos pos = origin.offset(dx, 0, dz);
			auto t = region.getBlockAndData(pos);
			if ((t.id == Block::mLeaves->mID && t.data == mLeafType) || _isFree(Block::mBlocks[t.id]->getMaterial())) {
				_setBlockAndData(region, pos, FullBlock(Block::mLeaves->mID, mLeafType));
			}
		}
	}
}

void MegaTreeFeature::_placeSingleTrunkLeaves(BlockSource& region, const BlockPos& origin, int radius) const {
	const int r2 = radius * radius;

	for (auto dx : range_incl(-radius, radius)) {
		for (auto dz : range_incl(-radius, radius)) {
			if (dx * dx + dz * dz > r2) {
				continue;
			}

			const BlockPos pos = origin.offset(dx, 0, dz);
			if (_isFree(region.getMaterial(pos))) {
				_setBlockAndData(region, pos, FullBlock(Block::mLeaves->mID, mLeafType));
			}
		}
	}
}

int MegaTreeFeature::_getBaseHeight() const {
	return mBaseHeight;
}

int MegaTreeFeature::_getHeightInterval() const {
	return mHeightInterval;
}

int MegaTreeFeature::_getTrunkType() const {
	return mTrunkType;
}

int MegaTreeFeature::_getLeafType() const {
	return mLeafType;
}

} // namespace vgen

