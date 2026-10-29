/********************************************************
*  CurseStone Server — vanilla terrain generator port   *
*         (c) CurseStone. All rights reserved.          *
*********************************************************/

#pragma once

#include "vgen/feature/Feature.h"

namespace vgen {

class FlowerFeature : public Feature {
public:

	FlowerFeature(BlockID block);
	virtual bool place(BlockSource& region, const BlockPos& pos, Random& random) const override;
	bool placeFlower(BlockSource& region, const BlockPos& pos, FullBlock flower, Random& random) const;

private:

	const FullBlock mBlock;
};

} // namespace vgen

