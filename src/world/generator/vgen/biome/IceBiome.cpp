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

#include "vgen/biome/IceBiome.h"
#include "vgen/Blocks.h"
#include "vgen/biome/BiomeDecorator.h"
#include "vgen/biome/MobSpawnUtils.h"
#include "vgen/VgenWorld.h"
#include "vgen/feature/Feature.h"

namespace vgen {

IceBiome::IceBiome(int id, bool superIcy) :
	Biome(id, Biome::BiomeType::Ice)
	, superIcy(superIcy) {
	if(superIcy) {
		mTopMaterial = Block::mSnow->mID;
	}
	else {
		decorator->treeCount = 0.01f;
	}
	_friendlies.clear();

	for (size_t i = 0; i < _enemies.size(); ++i) {
		if (_enemies[i].mobClassId == EntityType::Skeleton) {
			_enemies.erase(_enemies.begin() + i);
			break;
		}
	}
	
	_enemies.insert(_enemies.end(), MobSpawnerData(EntityType::Stray, EntityDefinitionIdentifier("minecraft", EntityTypeToString(EntityType::Stray), "change_to_skeleton"), 120, 1, 2));

	_friendlies.insert(_friendlies.end(), MobSpawnerData(EntityType::PolarBear, 1, 1, 2, MobSpawnUtils::spawnPolarBearHerd));
	_friendlies.insert(_friendlies.end(), MobSpawnerData(EntityType::Rabbit, EntityDefinitionIdentifier("minecraft", "rabbit", "in_snow"), 4, 1, 2));
}

float IceBiome::getCreatureProbability() {
	return Biome::getCreatureProbability();
}

void IceBiome::decorate(BlockSource* source, Random& random, const BlockPos& origin, bool legacy, float limit){
	if(superIcy) {
		for (auto i : range(3)) {
			UNUSED_VARIABLE(i);

			const int randX = origin.x + random.nextInt(16);
			const int randZ = origin.z + random.nextInt(16);
			BlockPos p = BlockPos(randX, 0, randZ);

			p.y = source->getHeightmap(p);

			decorator->iceSpikeFeature->place(*source, p, random);
		}

		for (auto i : range(2)) {
			UNUSED_VARIABLE(i);

			const int randX = origin.x + random.nextInt(16);
			const int randZ = origin.z + random.nextInt(16);
			BlockPos p = BlockPos(randX, 0, randZ);

			p.y = source->getHeightmap(p);

			decorator->icePatchFeature->place(*source, p, random);
		}
	}
	Biome::decorate(source, random, origin, legacy, limit);
}

const FeaturePtr& IceBiome::getTreeFeature(Random* random){
	return decorator->spruceTreeFeature;
}


} // namespace vgen

