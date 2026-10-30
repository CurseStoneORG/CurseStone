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

#include "vgen/biome/TaigaBiome.h"
#include "vgen/biome/BiomeDecorator.h"
#include "vgen/Vanilla.h"
#include "vgen/VgenWorld.h"
#include "vgen/VgenWorld.h"
#include "vgen/Blocks.h"
#include "vgen/feature/DoublePlantFeature.h"
#include "vgen/Blocks.h"

namespace vgen {

TaigaBiome::TaigaBiome(int id, int type) :
	Biome(id, Biome::BiomeType::Taiga)
	, type(type) {
	_friendlies.insert(_friendlies.end(), MobSpawnerData(EntityType::Wolf, 8, 4, 4));
	decorator->treeCount = 10;
	if(type == MEGA || type == MEGA_SPRUCE) {
		decorator->grassCount = 7;
		decorator->deadBushCount = 1;
		decorator->mushroomCount = 3;
	}
	else {
		decorator->grassCount = 1;
		decorator->mushroomCount = 1;
		_friendlies.insert(_friendlies.end(), MobSpawnerData(EntityType::Rabbit, 4, 2, 3));
	}
}

const FeaturePtr& TaigaBiome::getTreeFeature(Random* random){
	if((type == MEGA || type == MEGA_SPRUCE) && random->nextInt(3) == 0) {
		if(type == MEGA_SPRUCE || random->nextInt(13) == 0) {
			return decorator->megaSpruceTreeFeature;
		}
		return decorator->megaPineTreeFeature;
	}
	if(random->nextInt(3) == 0) {
		return decorator->pineTreeFeature;
	}
	return decorator->spruceTreeFeature;
}

const FeaturePtr& TaigaBiome::getGrassFeature(Random* random){
	if(random->nextInt(5) > 0) {
		return decorator->fernFeature;
	}
	return decorator->tallGrassFeature;
}

void TaigaBiome::decorate(BlockSource* source, Random& random, const BlockPos& origin, bool legacy, float limit){
	if (type == MEGA || type == MEGA_SPRUCE) {
		int stoneCount = random.nextInt(3);

		for (int i = 0; i < stoneCount; i++) {
			const int randZ = origin.x + random.nextInt(16);
			const int randX = origin.z + random.nextInt(16);
			BlockPos p = BlockPos(randX, 0, randZ);

			p.y = source->getHeightmap(p);

			decorator->forestRockFeature->place(*source, p, random);
		}
	}

	DataID fernID = enum_cast(DoublePlantType::Fern);

	const int randZ = origin.x + random.nextInt(16);
	const int randX = origin.z + random.nextInt(16);
	BlockPos p = BlockPos(randX, 0, randZ);

	auto pos = source->getHeightmapPos(p);

	decorator->doublePlantFeature->place(*source, pos, random, fernID);

	Biome::decorate(source, random, origin, legacy, limit);
}

void TaigaBiome::buildSurfaceAt(Random& random, BlockVolume& blocks, const BlockPos& pos, float depthValue, Height seaLevel) {
	if(type == MEGA || type == MEGA_SPRUCE) {
		mTopMaterial = Block::mGrass->mID;
		mTopMaterialData = 0;
		mMaterial = Block::mDirt->mID;
		if(depthValue > 1.75) {
			mTopMaterial = Block::mDirt->mID;
			Block::mDirt->getBlockState(BlockState::MappedType).set(mTopMaterialData, DirtBlock::TYPE_NO_GRASS_SPREAD);
		}
		else if(depthValue > -.95) {
			// Podzol is it's own type now instead of a dirt type
			mTopMaterial = Block::mPodzol->mID;
			mTopMaterialData = 0;
		}
	}
	buildSurfaceAtDefault(random, blocks, pos, depthValue, seaLevel);	// skip super
}


} // namespace vgen

