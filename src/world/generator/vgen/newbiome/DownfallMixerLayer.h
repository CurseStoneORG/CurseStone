/********************************************************
*  CurseStone Server — vanilla terrain generator port   *
*         (c) CurseStone. All rights reserved.          *
*********************************************************/
#pragma once

#include "vgen/newbiome/Layer.h"

namespace vgen {

class DownfallMixerLayer : public Layer {
public:

	DownfallMixerLayer(LayerPtr downfall, LayerPtr& parent, int layer);
	virtual void fillArea(LayerData& layerData, int xo, int yo, int w, int h) override;

protected:

	int mLayer;
	LayerPtr mDownfall;
};

} // namespace vgen

