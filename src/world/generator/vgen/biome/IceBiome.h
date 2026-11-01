#pragma once

#include "vgen/biome/Biome.h"

namespace vgen {

class IceBiome : public Biome {
public:

	IceBiome(int id, bool superIcy);
	void decorate(BlockSource* source, Random& random, const BlockPos& origin, bool legacy, float limit) override;
	const FeaturePtr& getTreeFeature(Random* random) override;

	virtual float getCreatureProbability() override;

private:

	bool superIcy;
};

} // namespace vgen

