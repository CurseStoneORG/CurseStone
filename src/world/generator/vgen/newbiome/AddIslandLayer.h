/********************************************************
*   (c) Mojang. All rights reserved                     *
*   (c) Microsoft. All rights reserved.                 *
*********************************************************/
#pragma once

#include "vgen/newbiome/Layer.h"

namespace vgen {

class AddIslandLayer : public Layer {
public:

	AddIslandLayer(RandomSeed seedMixup, LayerPtr& parent);
	virtual void fillArea(LayerData& layerData, int xo, int yo, int w, int h) override;

};

} // namespace vgen

