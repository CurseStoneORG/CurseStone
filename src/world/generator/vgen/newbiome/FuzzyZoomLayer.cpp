/********************************************************
*   (c) Mojang. All rights reserved                     *
*   (c) Microsoft. All rights reserved.                 *
*********************************************************/

#include "vgen/newbiome/FuzzyZoomLayer.h"

#include "vgen/biome/Biome.h"

namespace vgen {

FuzzyZoomLayer::FuzzyZoomLayer(RandomSeed seedMixup, LayerPtr& parent) 
	: ZoomLayer(seedMixup, parent) {

}

LayerPtr FuzzyZoomLayer::zoom(RandomSeed seed, LayerPtr& sup, int count) {
	LayerPtr result = sup;

	for(auto i : range(count)) {
		result = make_shared<FuzzyZoomLayer>(seed + i, result);
	}
	return result;
}

int FuzzyZoomLayer::_modeOrRandom(const int a, const int b, const int c, const int d) {
	int randomData[] = {
		a, b, c, d
	};
	return _random(randomData, 4);
}

} // namespace vgen

