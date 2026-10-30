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

#include "vgen/biome/ExtremeHillsBiome.h"
#include "vgen/Blocks.h"
#include "vgen/biome/BiomeDecorator.h"
#include "vgen/VgenWorld.h"
#include "vgen/VgenWorld.h"
#include "vgen/Vanilla.h"
#include "vgen/feature/OreFeature.h"
#include "vgen/Vanilla.h"

namespace vgen {

const bool ExtremeHillsBiome::GENERATE_EMERALD_ORE = true;

ExtremeHillsBiome::ExtremeHillsBiome(int id, bool extraTrees) :
	Biome(id, Biome::BiomeType::ExtremeHills) {
	silverFishFeature = make_unique<OreFeature>(Block::mMonsterStoneEgg->mID, 8);
	if(extraTrees) {
		decorator->treeCount = 3;
		type = TYPE_EXTRA_TREES;
	}
	else {
		type = TYPE_NORMAL;
	}
}

const FeaturePtr& ExtremeHillsBiome::getTreeFeature(Random* random){
	if(random->nextInt(3) > 0) {
		return decorator->spruceTreeFeature;
	}
	return Biome::getTreeFeature(random);
}

void ExtremeHillsBiome::decorate(BlockSource* source, Random& random, const BlockPos& origin, bool legacy, float limit){
	Biome::decorate(source, random, origin, legacy, limit);
	if(GENERATE_EMERALD_ORE) {
		int emeraldCount = 3 + random.nextInt(6);

		for (auto i : range(emeraldCount)) {
			UNUSED_VARIABLE(i);
			const int x = random.nextInt(16);
			const int y = random.nextInt(32 - 4) + 4;
			const int z = random.nextInt(16);

			BlockPos pos = origin.offset(x, y, z);
			if(source->getBlockID(pos) == Block::mStone->mID) {
				source->setBlockAndData(pos, Block::mEmeraldOre->mID, Block::UPDATE_CLIENTS);
			}
		}
	}

	for (auto i : range(7)) {
		UNUSED_VARIABLE(i);
		int x = random.nextInt(16);
		int y = random.nextInt(32);
		int z = random.nextInt(16);
		silverFishFeature->place(*source, origin.offset(x, y, z), random);
	}
}

void ExtremeHillsBiome::buildSurfaceAt(Random& random, BlockVolume& blocks, const BlockPos& pos, float depthValue, Height seaLevel) {
	mTopMaterial = Block::mGrass->mID;
	mTopMaterialData = 0;
	mMaterial = Block::mDirt->mID;
	if((depthValue < -0.1f || depthValue > 0.2f) && type == TYPE_MUTATED) {
		mTopMaterial = Block::mGravel->mID;
		mMaterial = Block::mGravel->mID;
	}
	else if(depthValue > 1.0 && type != TYPE_EXTRA_TREES) {
		mTopMaterial = Block::mStone->mID;
		mMaterial = Block::mStone->mID;
	}
	buildSurfaceAtDefault(random, blocks, pos, depthValue, seaLevel);	// skip super
}

ExtremeHillsBiome* ExtremeHillsBiome::setMutated(Biome* parent) {
	type = TYPE_MUTATED;

	setColor(parent->mDebugMapColor, true);
	setName(parent->mName + " M");
	setDepthAndScale(BiomeHeight(parent->mDepth, parent->mScale));
	setTemperatureAndDownfall(parent->mTemperature, parent->mDownfall);
	return this;
}


} // namespace vgen

