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
#include "vgen/feature/HellSpringFeature.h"

#include "vgen/VgenWorld.h"
#include "vgen/Blocks.h"
#include "vgen/material/Material.h"

namespace vgen {

HellSpringFeature::HellSpringFeature(BlockID blockId, bool insideRock) :
	Feature()
	, mBlockId(blockId)
	, mInsideRock(insideRock) {
}

bool HellSpringFeature::place(BlockSource& region, const BlockPos& pos, Random& random) const {
	if (region.getBlockID(pos.above()) != Block::mNetherrack->mID) {
		return false;
	}

	if (!region.getMaterial(pos).isType(MaterialType::Air) &&
		region.getBlockID(pos) != Block::mNetherrack->mID) {
		return false;
	}

	int rockCount = 0;
	if (region.getBlockID(pos.west()) == Block::mNetherrack->mID) {
		rockCount++;
	}
	if (region.getBlockID(pos.east()) == Block::mNetherrack->mID) {
		rockCount++;
	}
	if (region.getBlockID(pos.north()) == Block::mNetherrack->mID) {
		rockCount++;
	}
	if (region.getBlockID(pos.south()) == Block::mNetherrack->mID) {
		rockCount++;
	}
	if (region.getBlockID(pos.below()) == Block::mNetherrack->mID) {
		rockCount++;
	}

	int holeCount = 0;
	if (region.isEmptyBlock(pos.west())) {
		holeCount++;
	}
	if (region.isEmptyBlock(pos.east())) {
		holeCount++;
	}
	if (region.isEmptyBlock(pos.north())) {
		holeCount++;
	}
	if (region.isEmptyBlock(pos.south())) {
		holeCount++;
	}
	if (region.isEmptyBlock(pos.below())) {
		holeCount++;
	}

	if ((!mInsideRock && rockCount == 4 && holeCount == 1) || rockCount == 5) {
		_setBlockAndData(region, pos, FullBlock(mBlockId));
	}

	return true;
}

} // namespace vgen
