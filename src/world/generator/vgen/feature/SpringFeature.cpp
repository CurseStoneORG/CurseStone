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

#include "vgen/feature/SpringFeature.h"
#include "vgen/VgenWorld.h"
#include "vgen/Blocks.h"
#include "vgen/Random.h"

namespace vgen {

SpringFeature::SpringFeature(BlockID block) :
	mBlock(block) {

}

bool SpringFeature::place(BlockSource& region, const BlockPos& pos, Random& random) const {
	if (region.getBlockID(pos.above()) != Block::mStone->mID) {
		return false;
	}
	if (region.getBlockID(pos.below()) != Block::mStone->mID) {
		return false;
	}

	if (region.getBlockID(pos) != 0 && region.getBlockID(pos) != Block::mStone->mID) {
		return false;
	}

	int rockCount = 0;
	if (region.getBlockID(pos.west()) == Block::mStone->mID) {
		rockCount++;
	}
	if (region.getBlockID(pos.east()) == Block::mStone->mID) {
		rockCount++;
	}
	if (region.getBlockID(pos.north()) == Block::mStone->mID) {
		rockCount++;
	}
	if (region.getBlockID(pos.south()) == Block::mStone->mID) {
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

	if (rockCount == 3 && holeCount == 1 && _setBlockAndData( region, pos, mBlock)) {
		Block::mBlocks[mBlock]->tick(region, pos, random);
	}

	return true;
}

} // namespace vgen

