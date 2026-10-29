/********************************************************
*  CurseStone Server — vanilla terrain generator port   *
*         (c) CurseStone. All rights reserved.          *
*********************************************************/

#pragma once

#include "vgen/feature/Feature.h"

namespace vgen {

class TallGrassFeature : public Feature {
public:

	TallGrassFeature(BlockID block, DataID type);
	bool place(BlockSource& region, const BlockPos& pos, Random& random, int tries, int r) const;
	virtual bool place(BlockSource& region, const BlockPos& pos, Random& random) const override;

private:

	FullBlock mGrass;
};

} // namespace vgen

