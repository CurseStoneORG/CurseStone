#pragma once

#include "vgen/biome/Biome.h"

namespace vgen {

class RiverBiome : public Biome {
public:

	RiverBiome(int id) :
		Biome(id, Biome::BiomeType::River) {
		_friendlies.clear();
	}

};

} // namespace vgen

