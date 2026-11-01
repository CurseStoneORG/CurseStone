/********************************************************
*   (c) Mojang. All rights reserved                     *
*   (c) Microsoft. All rights reserved.                 *
*********************************************************/
#pragma once

#include "vgen/newbiome/Layer.h"

namespace vgen {

class SmoothZoomLayer : public Layer {
public:

	SmoothZoomLayer(RandomSeed seedMixup, LayerPtr& parent);
	virtual void fillArea(LayerData& layerData, int xo, int yo, int w, int h) override;
	static LayerPtr zoom(RandomSeed seed, LayerPtr& sup, int count);

};

} // namespace vgen

