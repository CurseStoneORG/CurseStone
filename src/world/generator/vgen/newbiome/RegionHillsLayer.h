/********************************************************
*   (c) Mojang. All rights reserved                     *
*   (c) Microsoft. All rights reserved.                 *
*********************************************************/
#pragma once

#include "vgen/newbiome/Layer.h"

namespace vgen {

class RegionHillsLayer : public Layer {
public:

	RegionHillsLayer(RandomSeed seedMixup, LayerPtr& parent, LayerPtr& riverLayer);
	virtual void fillArea(LayerData& layerData, int xo, int yo, int w, int h) override;

protected:

	LayerPtr mRiverLayer;
};

} // namespace vgen

