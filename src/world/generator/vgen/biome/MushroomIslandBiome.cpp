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

#include "vgen/biome/MushroomIslandBiome.h"
#include "vgen/biome/BiomeDecorator.h"
#include "vgen/Blocks.h"

namespace vgen {

MushroomIslandBiome::MushroomIslandBiome(int id) :
	Biome(id, Biome::BiomeType::MushroomIsland) {
	decorator->treeCount = -100;
	decorator->flowerCount = -100;
	decorator->grassCount = -100;

	decorator->mushroomCount = 1;
	decorator->hugeMushrooms = 1;

	mTopMaterial = Block::mMycelium->mID;

	_enemies.clear();
	_friendlies.clear();
	_waterFriendlies.clear();
	_friendlies.insert(_friendlies.end(), MobSpawnerData(EntityType::MushroomCow, 8, 4, 8));
	//friendlies.add(new MobSpawnerData(MushroomCow.class, 8, 4, 8));
}

} // namespace vgen

