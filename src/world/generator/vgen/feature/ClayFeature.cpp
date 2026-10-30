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

#include "vgen/feature/ClayFeature.h"
#include "vgen/Blocks.h"
#include "vgen/VgenWorld.h"
#include "vgen/material/Material.h"
#include "vgen/Random.h"

namespace vgen {

ClayFeature::ClayFeature(int radius) :
	mRadius(radius) {

}

bool ClayFeature::place(BlockSource& region, const BlockPos& pos, Random& random) const {
	const Block& block = region.getBlock(pos);
	auto& mat = block.getMaterial();
	if (!mat.isType(MaterialType::Water) && !block.isType(Block::mClay)) {
		return false;
	}

	int r = random.nextInt(mRadius - 2) + 2;
	int yr = 1;

	for (auto xx : range_incl(pos.x - r, pos.x + r)) {
		for (auto zz : range_incl(pos.z - r, pos.z + r)) {
			int xd = xx - pos.x;
			int zd = zz - pos.z;
			if (xd * xd + zd * zd > r * r) {
				continue;
			}

			for (auto yy : range_incl(pos.y - yr, pos.y + yr)) {
				BlockPos clayPos(xx, yy, zz);
				BlockID targetBlock = region.getBlockID(clayPos);
				if (targetBlock == Block::mDirt->mID) {
					_setBlockAndData( region, clayPos, Block::mClay->mID);
				}
			}
		}
	}

	return true;
}

} // namespace vgen

