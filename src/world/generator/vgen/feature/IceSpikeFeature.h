/********************************************************
*  CurseStone Server — vanilla terrain generator port   *
*         (c) CurseStone. All rights reserved.          *
*********************************************************/

#pragma once

#include "vgen/feature/Feature.h"

namespace vgen {

class IceSpikeFeature : public Feature {
public:

	virtual bool place(BlockSource& region, const BlockPos& pos, Random& random) const override;

private:

	bool _isValidPlaceBlock(BlockID block) const;
};

} // namespace vgen

