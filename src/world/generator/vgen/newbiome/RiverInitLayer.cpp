/********************************************************
*  CurseStone Server — vanilla terrain generator port   *
*         (c) CurseStone. All rights reserved.          *
*********************************************************/

#include "vgen/newbiome/RiverInitLayer.h"

#include "vgen/biome/Biome.h"

namespace vgen {

RiverInitLayer::RiverInitLayer(RandomSeed seedMixup, LayerPtr& parent)
	: Layer(seedMixup) {
	mParent = parent;
}

void RiverInitLayer::fillArea(LayerData& layerData, int xo, int yo, int w, int h) {
	mParent->fillArea(layerData, xo, yo, w, h);

	for(auto y : range(h)) {
		for(auto x : range(w)) {
			initRandom(x + xo, y + yo);
			layerData.mResult[x + y * w] = layerData.mParentArea[x + y * w] > 0 ? nextRandom(299999) + 2 : 0;
		}
	}
	layerData.swap();
}

} // namespace vgen

