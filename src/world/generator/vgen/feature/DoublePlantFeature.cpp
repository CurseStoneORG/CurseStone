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

#include "vgen/feature/DoublePlantFeature.h"
#include "vgen/Random.h"
#include "vgen/VgenWorld.h"
#include "vgen/Blocks.h"
#include "vgen/VgenWorld.h"
#include "vgen/Vanilla.h"

namespace vgen {

bool DoublePlantFeature::place(BlockSource& region, const BlockPos& pos, Random& random) const {
	return place(region, pos, random, enum_cast(DoublePlantType::Grass));
}

bool DoublePlantFeature::place(BlockSource& region, const BlockPos& pos, Random& random, int plantType) const {
	bool placedPlant = false;

	for (auto i : range(64)) {
		UNUSED_VARIABLE(i);
		const int randZ = random.nextGaussianInt(8);
		const int randY = random.nextGaussianInt(4);
		const int randX = random.nextGaussianInt(8);
		BlockPos plantPos = pos.offset(randX, randY, randZ);

		if (region.isEmptyBlock(plantPos) && plantPos.y < (region.getMaxHeight() - 1 - 1)) {
			if (Block::mDoublePlant->mayPlace(region, plantPos)) {
				static_cast<const DoublePlantBlock*>(Block::mDoublePlant)->placeAt(region, plantPos, plantType, Block::UPDATE_CLIENTS, nullptr);
				placedPlant = true;
			}
		}
	}

	return placedPlant;
}

} // namespace vgen

