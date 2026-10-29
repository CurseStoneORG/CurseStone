/********************************************************
*  CurseStone Server — vanilla terrain generator port   *
*         (c) CurseStone. All rights reserved.          *
*********************************************************/
#pragma once

#include "vgen/newbiome/Layer.h"

namespace vgen {

class IslandLayer : public Layer {
public:

	IslandLayer(RandomSeed seedMixup);
	virtual void fillArea(LayerData& layerData, int xo, int yo, int w, int h) override;

};

} // namespace vgen

