#pragma once

#include "vgen/biome/Biome.h"

namespace vgen {

class PlainsBiome : public Biome {
public:

	PlainsBiome(int id);

	FullBlock getRandomFlowerTypeAndData(Random& random, const BlockPos& pos) override;
	void decorate(BlockSource* source, Random& random, const BlockPos& origin, bool legacy, float limit) override;

	virtual FeaturePtr& getTreeFeature(Random* random) override;

public:

	bool sunflowers = false;
};

} // namespace vgen

