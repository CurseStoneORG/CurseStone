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

#include "vgen/Vanilla.h"
#include "vgen/newbiome/Layer.h"
#include "vgen/Vanilla.h"
#include "vgen/Vanilla.h"

namespace vgen {

class Level;
class Biome;
class ChunkPos;
class LevelChunk;
class Layer;
class LayerData;

class BiomeSource {
public:

	BiomeSource(RandomSeed seed);
	BiomeSource(RandomSeed seed, GeneratorType type);
	virtual ~BiomeSource() {
	}

	virtual void fillBiomeData(LevelChunk* levelChunk, int x, int z) const;
	virtual void fillRawBiomeData(Biome** biomes, int x, int z) const;


	virtual BlockPos getSpawnPosition() const;

	virtual bool findValidSpawnPosition(LayerData& layerData, int x, int z, BlockPos& spawnPosition) const;
	virtual bool isValidSpawn(int currentBiome) const;

	virtual bool containsOnly(int x, int z, int r, const std::vector<int>& allowed);

	virtual Biome* getBiome(int blockX, int blockZ);
	virtual Biome* getBiome(const BlockPos& block);

	RandomSeed getSeed() {
		return seed;
	}

private:

	LayerPtr getZoomedLayer();

	LayerPtr biomeLayer;
	LayerPtr zoomedLayer;
	std::array<int, 7> playerValidSpawns;
	RandomSeed seed;
};

} // namespace vgen

