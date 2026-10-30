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

#include "vgen/biome/SwampBiome.h"
#include "vgen/biome/BiomeDecorator.h"
#include "vgen/synth/PerlinSimplexNoise.h"
#include "vgen/Vanilla.h"
#include "vgen/Blocks.h"
#include "vgen/Vanilla.h"
#include "vgen/VgenWorld.h"
#include "vgen/Vanilla.h"

namespace vgen {

SwampBiome::SwampBiome(int id)
	: Biome(id, Biome::BiomeType::Swamp) {
	decorator->treeCount = 2;
	decorator->flowerCount = 1;
	decorator->deadBushCount = 1;
	decorator->mushroomCount = 8;
	decorator->reedsCount = 10;
	decorator->clayCount = 1;
	decorator->waterlilyCount = 4;
	decorator->sandCount = 0;
	decorator->gravelCount = 0;
	decorator->grassCount = 5;

	mWaterColor = Color::fromARGB(0x00c8af4b);
	mWaterColor.a = 0.8f;
	mUnderWaterColor = Color::fromARGB(0x001d1d21);
	mUnderWaterColor.a = 0.75f;

	// increase the chance of slimes in swamps
	_enemies.insert(_enemies.end(), MobSpawnerData(EntityType::Slime, 1, 1, 1));
}

const FeaturePtr& SwampBiome::getTreeFeature(Random* random){
	if (random->nextInt(20) == 0) {
		return decorator->hugeMushroomFeature;
	}
	else{
		return decorator->swampTreeFeature;
	}
}

int SwampBiome::getGrassColor( const BlockPos& pos ) {
	//jasonmaj $TODO Need to data drive this somehow. Ideally find spot in grass.png
	float groundValue = mBiomeInfoNoise->getValue(pos.x * 0.0225f, pos.z * 0.0225f);
	if(groundValue < -0.1f) {
		return 0x4c763c;
	}
	return 0x6a7039;
}

int SwampBiome::getMapGrassColor(const BlockPos& pos) {
	float groundValue = mBiomeInfoNoise->getValue(pos.x * 0.0225f, pos.z * 0.0225f);
	if (groundValue < -0.1f) {
		return 0x4c763c;
	}
	return 0x6a7039;
}

int SwampBiome::getFoliageColor() {
	return 0x6a7039;
}

int SwampBiome::getMapFoliageColor() {
	//jasonmaj $TODO Need to data drive this somehow. Ideally find spot in foliage.png
	return 0x6a7039;
}

FullBlock SwampBiome::getRandomFlowerTypeAndData(Random& random, const BlockPos& pos) {
	return FullBlock(Block::mRedFlower->mID, (DataID)FlowerBlock::ROSES::ORCHID);
}

void SwampBiome::buildSurfaceAt(Random& random, BlockVolume& blocks, const BlockPos& pos, float depthValue, Height seaLevel) {
	float groundValue = mBiomeInfoNoise->getValue(pos.x * 0.25f, pos.z * 0.25f);
	if(groundValue > 0) {
		ChunkBlockPos p(pos);
		for(auto y : reverse_range(LEVEL_GEN_HEIGHT)) {
			p.y = y;
			auto block = blocks.block(p);
			auto& mat = Block::mBlocks[block]->getMaterial();

			if(mat.isType(MaterialType::Air)) {
				if(y == 62 && block != Block::mStillWater->mID) {
					blocks.setBlock(p, Block::mStillWater->mID);
					if(groundValue < 0.12f) {
						blocks.setBlock(p.above(), Block::mWaterlily->mID);
					}
				}
				break;
			}
		}
	}

	buildSurfaceAtDefault(random, blocks, pos, depthValue, seaLevel);
}

} // namespace vgen

