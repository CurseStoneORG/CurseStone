#pragma once

#include "vgen/biome/Biome.h"

namespace vgen {

class JungleBiome : public Biome {
public:

	JungleBiome(int id, bool isEdge);
	const FeaturePtr& getTreeFeature(Random* random) override;
	const FeaturePtr& getGrassFeature(Random* random) override;
	void decorate(BlockSource* source, Random& random, const BlockPos& origin, bool legacy, float limit) override;

protected:

	bool isEdge;
};

} // namespace vgen

