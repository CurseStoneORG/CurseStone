#pragma once

#include "vgen/biome/Biome.h"

namespace vgen {

class ExtremeHillsBiome : public Biome {
	static const bool GENERATE_EMERALD_ORE;
public:

	ExtremeHillsBiome(int id, bool extraTrees);
	const FeaturePtr& getTreeFeature(Random* random) override;
	void decorate(BlockSource* source, Random& random, const BlockPos& origin, bool legacy, float limit) override;
	void buildSurfaceAt(Random& random, BlockVolume& blocks, const BlockPos& pos, float depthValue, Height seaLevel) override;

protected:

	ExtremeHillsBiome* setMutated(Biome* parent);

private:

	static const int TYPE_NORMAL = 0;
	static const int TYPE_EXTRA_TREES = 1;
	static const int TYPE_MUTATED = 2;
	int type;
	FeaturePtr silverFishFeature;
};

} // namespace vgen

