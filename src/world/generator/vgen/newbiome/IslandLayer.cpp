/********************************************************
*  CurseStone Server — vanilla terrain generator port   *
*         (c) CurseStone. All rights reserved.          *
*********************************************************/

#include "vgen/newbiome/IslandLayer.h"

#include "vgen/biome/Biome.h"

namespace vgen {

IslandLayer::IslandLayer(RandomSeed seedMixup)
	: Layer(seedMixup) {
}

void IslandLayer::fillArea(LayerData& layerData, int xo, int yo, int w, int h) {
	for(auto y : range(h)) {
		for(auto x : range(w)) {
			initRandom(xo + x, yo + y);
			layerData.mResult[x + y * w] = (nextRandom(10) == 0) ? 1 : 0;
		}
	}
	// if (0, 0) is located here, place an island
	if(xo > -w && xo <= 0 && yo > -h && yo <= 0) {
		layerData.mResult[-xo + -yo * w] = 1;
	}
	layerData.swap();
}

} // namespace vgen

