/********************************************************
*  CurseStone Server — vanilla terrain generator port   *
*         (c) CurseStone. All rights reserved.          *
*********************************************************/
#pragma once

#include "vgen/newbiome/Layer.h"

namespace vgen {

class BiomeEdgeLayer : public Layer {
public:

	BiomeEdgeLayer(RandomSeed seedMixup, LayerPtr& parent, bool generateJungles = true);
	virtual void fillArea(LayerData& layerData, int xo, int yo, int w, int h) override;
	bool checkEdge(LayerData& layerData, int x, int y, int w, int old, int checkFor, int addEdge);
	bool checkEdgeStrict(LayerData& layerData, int x, int y, int w, int old, int checkFor, int addEdge);
	bool isValidTemperatureEdge(int a, int b);

protected:

	const bool mGenerateJungles;
};

} // namespace vgen

