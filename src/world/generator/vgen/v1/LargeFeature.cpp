
#include "vgen/v1/LargeFeature.h"
#include "vgen/VgenWorld.h"
#include "vgen/biome/BiomeSource.h"
#include "vgen/VgenWorld.h"
#include "vgen/VgenWorld.h"

namespace vgen {

LargeFeature::LargeFeature() :
	radius(8){
}

LargeFeature::~LargeFeature(){
}

void LargeFeature::apply(LevelChunk& lc, Dimension& dimension, Random& random){
	int r = radius;

	random.setSeed(dimension.getBiomes().getSeed());
	int xScale = random.nextInt() / 2 * 2 + 1;
	int zScale = random.nextInt() / 2 * 2 + 1;

	auto& lcPosition = lc.getPosition();
	for (int x = lcPosition.x - r; x <= lcPosition.x + r; x++) {
		for (int z = lcPosition.z - r; z <= lcPosition.z + r; z++) {
			random.setSeed((x * xScale + z * zScale) ^ dimension.getBiomes().getSeed());
			addFeature(lc, dimension, random, x , z );
		}
	}
}

} // namespace vgen

