/********************************************************
*   (c) Mojang. All rights reserved                     *
*   (c) Microsoft. All rights reserved.                 *
*********************************************************/
#pragma once

#include "vgen/newbiome/Layer.h"

namespace vgen {

class RiverLayer : public Layer {
public:

	RiverLayer(RandomSeed seedMixup, LayerPtr& parent);
	virtual void fillArea(LayerData& layerData, int xo, int yo, int w, int h) override;
	int riverFilter(int value);

};

} // namespace vgen

