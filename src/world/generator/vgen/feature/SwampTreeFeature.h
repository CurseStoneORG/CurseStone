/********************************************************
*  CurseStone Server — vanilla terrain generator port   *
*         (c) CurseStone. All rights reserved.          *
*********************************************************/

#pragma once

#include "vgen/feature/TreeFeature.h"

namespace vgen {

class SwampTreeFeature : public TreeFeature {
public:

	SwampTreeFeature();
	virtual bool place(BlockSource& region, const BlockPos& pos, Random& random) const override;

private:

	void _addVine(BlockSource& region, BlockPos pos, int dir) const;
};

} // namespace vgen

