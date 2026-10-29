/********************************************************
*  CurseStone Server — vanilla terrain generator port   *
*         (c) CurseStone. All rights reserved.          *
*********************************************************/
#pragma once

#include "vgen/newbiome/Layer.h"

namespace vgen {

class RiverMixerLayer : public Layer {
public:

	RiverMixerLayer(RandomSeed seedMixup, LayerPtr& biomes, LayerPtr& rivers);
	void init(int64_t seed) override;
	virtual void fillArea(LayerData& layerData, int xo, int yo, int w, int h) override;

protected:

	LayerPtr mBiomes;
	LayerPtr mRivers;
};

} // namespace vgen

