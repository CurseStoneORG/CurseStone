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

