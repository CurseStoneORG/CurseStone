/********************************************************
*  CurseStone Server — vanilla terrain generator port   *
*         (c) CurseStone. All rights reserved.          *
*********************************************************/

#pragma once

#include "vgen/feature/Feature.h"

namespace vgen {

class BlockBlobFeature : public Feature {
public:

	BlockBlobFeature(BlockID block, int radius);

	virtual bool place(BlockSource& region, const BlockPos& pos, Random& random) const override;

protected:

	FullBlock mBlock;
	int mStartRadius;
};

} // namespace vgen

