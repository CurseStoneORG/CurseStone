/********************************************************
*  (c) Mojang.    All rights reserved.                  *
*  (c) Microsoft. All rights reserved.                  *
*********************************************************/
#pragma once

#include "vgen/structure/StructureFeatureType.h"
#include "vgen/v1/LargeFeature.h"
#include "vgen/Vanilla.h"
#include "vgen/Vanilla.h"
#include "StructureStart.h"

namespace vgen {

class Player;
class BlockSource;
class LevelChunk;
class Dimension;

class StructureFeature : public LargeFeature {

	typedef std::unordered_map<ChunkPos, Unique<StructureStart> > StructureMap;

public:

	StructureFeature()
		: LargeFeature(){

	}

	virtual std::string getFeatureName() = 0;

	bool postProcess(BlockSource* level, Random& random, int chunkX, int chunkZ);

	void postProcessMobsAt(BlockSource* blockSource, int chunkWestBlock, int chunkNorthBlock, Random& random);

	bool isInsideFeature(int cellX, int cellY, int cellZ);
	bool isInsideBoundingFeature(int cellX, int cellY, int cellZ);

	virtual bool getNearestGeneratedFeature(Player& player, BlockPos& pos);

protected:

	virtual void addFeature(LevelChunk& lc, Dimension& generator, Random& random, int x, int z ) override;

	virtual bool isFeatureChunk(BiomeSource* biomeSource, Random& random, const ChunkPos& lc) = 0;

	virtual Unique<StructureStart> createStructureStart(Dimension& biomeSource, Random& random, const ChunkPos& lc) = 0;
	virtual StructureStart* getStructureAt(int cellX, int cellY, int cellZ);

	virtual std::vector<BlockPos> getGuesstimatedFeaturePositions();

	StructureMap cachedStructures;
	std::mutex cacheMutex;
};

} // namespace vgen

