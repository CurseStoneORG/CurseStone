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

#include "vgen/feature/Feature.h"
#include "vgen/Blocks.h"
#include "vgen/VgenWorld.h"

namespace vgen {

Feature::Feature(Entity* placer)
	: mPlacer(placer)
{
}

void Feature::_setManuallyPlaced(Entity* placer) { 
	mPlacer = placer; 
}

bool Feature::_getManuallyPlaced() const {
	return mPlacer != nullptr;
}

bool Feature::_placeBlock(BlockSource& region, const BlockPos& pos, const FullBlock& blockData) const {
	return region.setBlockAndData( pos, blockData, Block::UPDATE_ALL, mPlacer);
}

bool Feature::_setBlockAndData(BlockSource& region, const BlockPos& pos, const FullBlock& blockData) const {
	return region.setBlockAndData( pos, blockData, Block::UPDATE_ALL, mPlacer);
}

} // namespace vgen

