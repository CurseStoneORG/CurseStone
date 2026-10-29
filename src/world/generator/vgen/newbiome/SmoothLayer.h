#pragma once
#include "vgen/newbiome/Layer.h"

namespace vgen {

class SmoothLayer : public Layer {
public:

	SmoothLayer(RandomSeed seedMixup, LayerPtr& parent);
	virtual void fillArea(LayerData& layerData, int xo, int yo, int w, int h) override;

};

} // namespace vgen

