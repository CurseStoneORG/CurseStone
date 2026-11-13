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
#include "vgen/feature/GlowStoneFeature.h"

#include "vgen/VgenWorld.h"
#include "vgen/Blocks.h"
#include "vgen/material/Material.h"
#include "vgen/Random.h"

namespace vgen {

bool GlowStoneFeature::place(BlockSource& region, const BlockPos& pos, Random& random) const {
	if (!region.isEmptyBlock(pos)) {
		return false;
	}
	if (region.getBlockID(pos.above()) != Block::mNetherrack->mID) {
		return false;
	}

	_setBlockAndData(region, pos, FullBlock(Block::mGlowStone->mID));

	for (int i = 0; i < 1500; i++) {
		const int randZ = random.nextGaussianInt(8);
		const int randY = -random.nextInt(12);
		const int randX = random.nextGaussianInt(8);
		BlockPos gemPos = pos.offset(randX, randY, randZ);

		if (!region.getMaterial(gemPos).isType(MaterialType::Air)) {
			continue;
		}

		int count = 0;

		for (int t = 0; t < 6; t++) {
			BlockID block(BlockID::AIR);
			switch (t) {
			case 0:
				block = region.getBlockID(gemPos.west());
				break;
			case 1:
				block = region.getBlockID(gemPos.east());
				break;
			case 2:
				block = region.getBlockID(gemPos.below());
				break;
			case 3:
				block = region.getBlockID(gemPos.above());
				break;
			case 4:
				block = region.getBlockID(gemPos.north());
				break;
			case 5:
				block = region.getBlockID(gemPos.south());
				break;
			}

			if (block == Block::mGlowStone->mID) {
				count++;
			}
		}

		if (count == 1) {
			_setBlockAndData(region, gemPos, FullBlock(Block::mGlowStone->mID));
		}
	}

	return true;
}

} // namespace vgen
