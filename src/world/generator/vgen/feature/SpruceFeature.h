/********************************************************
*  CurseStone Server — vanilla terrain generator port   *
*         (c) CurseStone. All rights reserved.          *
*********************************************************/

#pragma once

#include "vgen/feature/TreeFeature.h"

namespace vgen {

class SpruceFeature : public TreeFeature {
public:

	SpruceFeature(Entity* manuallyPlaced = nullptr);
	virtual bool place(BlockSource& region, const BlockPos& pos, Random& random) const override;
};

} // namespace vgen

