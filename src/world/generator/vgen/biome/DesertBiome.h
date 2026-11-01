#pragma once

#include "vgen/biome/Biome.h"

namespace vgen {

class DesertBiome : public Biome {
public:

	DesertBiome(int id);
	void decorate(BlockSource* source, Random& random, const BlockPos& origin, bool legacy, float worldLimit) override;

};

} // namespace vgen

