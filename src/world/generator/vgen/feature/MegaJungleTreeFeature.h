/********************************************************
*  CurseStone Server — vanilla terrain generator port   *
*         (c) CurseStone. All rights reserved.          *
*********************************************************/

#pragma once

#include "vgen/feature/MegaTreeFeature.h"

namespace vgen {

/**
 * Same as tree feature, but slightly taller and white in color.
 */
class MegaJungleTreeFeature : public MegaTreeFeature {
public:

	MegaJungleTreeFeature(Entity* placer, int baseHeight, int heightInterval, int trunkType, int leafType);
	virtual bool place(BlockSource& region, const BlockPos& pos, Random& random) const override;

private:

	void _placeVines(BlockSource& region, const BlockPos& pos, Random& random, int height, int dirx, int dirz) const;
	void _placeVine(BlockSource& region, const BlockPos& pos, Random& random, int direction) const;
	void _createCrown(BlockSource& region, const BlockPos& pos, int baseRadius) const;
};

} // namespace vgen

