/********************************************************
*  CurseStone Server — vanilla terrain generator port   *
*         (c) CurseStone. All rights reserved.          *
*********************************************************/

#pragma once

#include "vgen/feature/TreeFeature.h"
#include "vgen/Vanilla.h"

namespace vgen {

class SavannaTreeFeature : public TreeFeature {
public:

	SavannaTreeFeature(Entity* manuallyPlaced = nullptr);
	virtual bool place(BlockSource& region, const BlockPos& pos, Random& random) const override;

private:

	void _placeLeafAt(BlockSource& region, const BlockPos& pos) const;
};

} // namespace vgen

