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

#include "vgen/biome/StoneBeachBiome.h"
#include "vgen/Blocks.h"
#include "vgen/biome/BiomeDecorator.h"

namespace vgen {

StoneBeachBiome::StoneBeachBiome(int id) :
	Biome(id, Biome::BiomeType::StoneBeach) {
	// remove default mob spawn settings
	_friendlies.clear();
	mTopMaterial = Block::mStone->mID;
	mMaterial = Block::mStone->mID;

	decorator->treeCount = -999;
	decorator->deadBushCount = 0;
	decorator->reedsCount = 0;
	decorator->cactusCount = 0;
}

} // namespace vgen

