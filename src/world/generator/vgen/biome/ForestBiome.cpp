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

#include "vgen/biome/ForestBiome.h"
#include "vgen/VgenWorld.h"
#include "vgen/biome/BiomeDecorator.h"
#include "vgen/Vanilla.h"
#include "vgen/Blocks.h"
#include "vgen/synth/PerlinSimplexNoise.h"
#include "vgen/Blocks.h"
#include "vgen/feature/Feature.h"
#include "vgen/feature/DoublePlantFeature.h"
#include "vgen/feature/TallGrassFeature.h"
#include "vgen/Blocks.h"

namespace vgen {

ForestBiome::ForestBiome(int id, int forestType) :
	Biome(id, Biome::BiomeType::Forest)
	, type(forestType) {
	decorator->treeCount = 10;
	decorator->grassCount = 0;
	if(type == TYPE_FLOWER) {
		decorator->treeCount = 1;
		decorator->flowerCount = 100;
		decorator->grassCount = 1;
	}
	setLeafColor(0x4EBA31);
	setTemperatureAndDownfall(0.7f, 0.8f);

	if(type == TYPE_BIRCH) {
		mDebugMapOddColor = 0x056621;
		mDebugMapColor = 0x307444;
		setTemperatureAndDownfall(0.6f, 0.6f);
	}

	if(type == TYPE_NORMAL) {
		_friendlies.insert(_friendlies.end(), MobSpawnerData(EntityType::Sheep, 5, 4, 4));
	}

	if(type == TYPE_ROOFED) {
		decorator->treeCount = -999;
	}
}

Biome& ForestBiome::setColor(int color, bool oddColor) {
	if(type == TYPE_BIRCH) {
		mDebugMapOddColor = 0x056621;
		mDebugMapColor = color;

		if(oddColor) {
			mDebugMapOddColor = (mDebugMapOddColor & 0xfefefe) >> 1;
		}
		return *this;
	}
	else {
		return Biome::setColor(color, oddColor);
	}
}

Biome& ForestBiome::setColor(int color) {
	return Biome::setColor(color);
}

const FeaturePtr& ForestBiome::getTreeFeature(Random* random){
	if(type == TYPE_ROOFED && random->nextInt(3) == 0) {
		return decorator->roofTreeFeature;
	}
	if(type == TYPE_BIRCH || random->nextInt(5) == 0) {
		return decorator->birchFeature;
	}
	return Biome::getTreeFeature(random);
}

FullBlock ForestBiome::getRandomFlowerTypeAndData(Random& random, const BlockPos& pos) {
	if(type == TYPE_FLOWER) {
		FullBlock flower;
		float placementValue = Math::clamp((1.0f + mBiomeInfoNoise->getValue(pos.x / 48.0f, pos.z / 48.0f)) / 2.0f, 0.0f, 0.9999f);
		flower.data = (int)(placementValue * (int)FlowerBlock::ROSES::_count);
		if(flower.data == (int)FlowerBlock::ROSES::ORCHID) {
			flower.data = (int)FlowerBlock::ROSES::POPPY;
		}
		flower.id = Block::mRedFlower->mID;
		//flower.data = flower.data;
		//HACK self-assignment???

		return flower;
	}
	else {
		return Biome::getRandomFlowerTypeAndData(random, pos);
	}
}

bool ForestBiome::isHumid(){
	if (type == TYPE_ROOFED) {
		return true;
	}
	return Biome::isHumid();
}

void ForestBiome::decorate(BlockSource* source, Random& random, const BlockPos& origin, bool legacy, float limit){
	if(type == TYPE_ROOFED) {
		for(auto bx : range(4)) {
			for(auto bz : range(4)) {
				const int x = bx * 4 + 1 + random.nextInt(3);
				const int z = bz * 4 + 1 + random.nextInt(3);

				BlockPos pos = origin.offset(x, 0, z);
				pos.y = source->getHeightmap(pos.x, pos.z);
				if(random.nextInt(20) == 0) {
					decorator->hugeMushroomFeature->place(*source, pos, random);
				}
				else {
					auto& tree = getTreeFeature(&random);
					tree->place(*source, pos, random );

					//place more grass around trees
					if (!legacy) {
						TallGrassFeature(Block::mTallgrass->mID, enum_cast(TallGrassType::Tall)).place(*source, pos, random, 20, 3);
					}

//                  tree.init();
//                  if(tree.place(level, random, pos)) {
//                      tree.postPlaceTree(level, random, pos);
//                  }
				}
			}
		}
	}
	int doublePlantCount = random.nextInt(5) - 3;
	if(type == TYPE_FLOWER) {
		doublePlantCount += 2;
	}

	if (doublePlantCount > 0) {
		for (auto i : range(doublePlantCount)) {
			UNUSED_VARIABLE(i);
			int select = random.nextInt(3);
			DoublePlantType plantType = DoublePlantType::Paeonia;
			if (select == 0) {
				plantType = DoublePlantType::Syringa;
			} else if (select == 1) {
				plantType = DoublePlantType::Rose;
			}

			for (auto j : range(5)) {
				UNUSED_VARIABLE(j);
				const int x = random.nextInt(16);
				const int z = random.nextInt(16);
				BlockPos offset = origin.offset(x, 0, z);
				const int y = random.nextInt(source->getHeightmap(offset.x, offset.z) + 32);

				if (decorator->doublePlantFeature->place(*source, BlockPos(origin.x + x, y, origin.z + z), random, enum_cast(plantType))) {
					break;
				}
			}
		}
	}
	Biome::decorate(source, random, origin, legacy, limit);
}


} // namespace vgen

