/********************************************************
*  CurseStone Server — vanilla terrain generator port   *
*         (c) CurseStone. All rights reserved.          *
*********************************************************/

#pragma once

#include "vgen/feature/Feature.h"

namespace vgen {

class HugeMushroomFeature : public Feature {
public:

	HugeMushroomFeature();
	HugeMushroomFeature(int forcedType);

	virtual bool place(BlockSource& region, const BlockPos& pos, Random& random) const override;

private:

	bool _canSurvive(BlockID belowBlock) const;

	int mForcedType;
};

} // namespace vgen

