#pragma once

#include "vgen/biome/Biome.h"

namespace vgen {

class SavannaBiome : public Biome {
public:

	SavannaBiome(int id);
	const FeaturePtr& getTreeFeature(Random* random) override;
	void decorate(BlockSource* source, Random& random, const BlockPos& origin, bool legacy, float limit) override;

};

} // namespace vgen

