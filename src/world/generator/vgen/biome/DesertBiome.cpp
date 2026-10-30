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

#include "vgen/biome/DesertBiome.h"
#include "vgen/Blocks.h"
#include "vgen/Blocks.h"
#include "vgen/biome/BiomeDecorator.h"
#include "vgen/VgenWorld.h"
#include "vgen/feature/Feature.h"
#include "vgen/Vanilla.h"

namespace vgen {

DesertBiome::DesertBiome(int id) :
	Biome(id, Biome::BiomeType::Desert) {
	_friendlies.clear();
	_friendlies.insert(_friendlies.end(), MobSpawnerData(EntityType::Rabbit, EntityDefinitionIdentifier("minecraft", EntityTypeToString(EntityType::Rabbit), "in_desert"), 4, 2, 3));

	_enemies.insert(_enemies.end(), MobSpawnerData(EntityType::Husk, 240, 2, 4));

	mTopMaterial = Block::mSand->mID;
	mTopMaterialData = enum_cast(SandBlock::SandType::Normal);
	mMaterial = Block::mSand->mID;

	decorator->treeCount = -999;
	decorator->deadBushCount = 2;
	decorator->reedsCount = 50;
	decorator->cactusCount = 10;
}

void DesertBiome::decorate( BlockSource* source, Random& random, const BlockPos& origin, bool legacy, float worldLimit ) {
	Biome::decorate(source, random, origin, legacy, worldLimit);

	if(random.nextInt(500) == 0) {
		const int randZ = random.nextInt(16);
		const int randX = random.nextInt(16);
		BlockPos p = BlockPos(randX, 0, randZ);

		p.y = source->getHeightmap(p);

		decorator->desertWellFeature->place(
			*source,
			origin.offset(p.x, LEVEL_GEN_HEIGHT, p.z),
			random );
	}
}

} // namespace vgen

