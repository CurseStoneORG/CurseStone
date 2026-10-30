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

#pragma once

#include "StructureFeature.h"
#include "StructureStart.h"
#include "vgen/VgenWorld.h"
#include "vgen/biome/MobSpawnerData.h"

namespace vgen {

class BiomeSource;
class Random;
class ChunkPos;

class ScatteredFeatureStart : public StructureStart {
public:
	ScatteredFeatureStart();
	ScatteredFeatureStart(int x, int z);

	ScatteredFeatureStart(Dimension& region, Random& random, int chunkX, int chunkZ);

	StructureFeatureType getType() const override {
		return StructureFeatureType::Temple;
	}
};

class RandomScatteredLargeFeature : public StructureFeature {

public:
	RandomScatteredLargeFeature();
	
	virtual std::string getFeatureName() override;
	std::vector<int> allowedBiomes;

	const MobList& getEnemiesAt(const BlockPos& pos);

protected:
	virtual bool isFeatureChunk(BiomeSource* biomeSource, Random& random, const ChunkPos& pos) override;

	virtual Unique<StructureStart> createStructureStart(Dimension& generator, Random& random, const ChunkPos& cp) override;

private:
	int mSpacing = 32;
	int mMinSeparation = 8;
	MobList mEmptyEnemies;
	MobList mWitchHutEnemies;
};

} // namespace vgen

