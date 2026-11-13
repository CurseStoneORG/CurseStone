/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

 * CurseStone | Open Source Server Software
 *
 * Copyright (C) 2026 CurseStoneORG
 *
 * Developed by CurseStoneORG.
 * Built for an open and extensible Minecraft server ecosystem.
 *
 * Licensed under the GNU Lesser General Public License v3.0 or later.
 * You may study, modify, and redistribute this software under its license.
 *
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 * Repository: https://github.com/CurseStoneORG/CurseStone
 *
 */
#pragma once

#include "vgen/biome/BiomeSource.h"
#include "vgen/biome/Biome.h"

namespace vgen {

class FixedBiomeSource : public BiomeSource {

public:

	FixedBiomeSource(RandomSeed seed, int biomeId);

	virtual void fillBiomeData(LevelChunk* levelChunk, int x, int z) const override;

	virtual void fillRawBiomeData(Biome** biomes, int x, int z) const override {
		UNUSED_PARAMETER(biomes);
		UNUSED_PARAMETER(x);
		UNUSED_PARAMETER(z);
	}

	virtual BlockPos getSpawnPosition() const override {
		return BlockPos();
	}

	virtual bool findValidSpawnPosition(LayerData& layerData, int x, int z, BlockPos& spawnPosition) const override {
		UNUSED_PARAMETER(layerData);
		UNUSED_PARAMETER(x);
		UNUSED_PARAMETER(z);
		UNUSED_PARAMETER(spawnPosition);
		return false;
	}

	virtual bool isValidSpawn(int currentBiome) const override {
		UNUSED_PARAMETER(currentBiome);
		return false;
	}

	virtual bool containsOnly(int x, int z, int r, const std::vector<int>& allowed) override {
		UNUSED_PARAMETER(x);
		UNUSED_PARAMETER(z);
		UNUSED_PARAMETER(r);
		UNUSED_PARAMETER(allowed);
		return false;
	}

	virtual Biome* getBiome(int blockX, int blockZ) override;
	virtual Biome* getBiome(const BlockPos& block) override;

private:

	int biomeId;
};

} // namespace vgen
