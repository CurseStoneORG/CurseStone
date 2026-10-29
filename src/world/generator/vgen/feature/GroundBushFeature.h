/********************************************************
*   (c) Mojang. All rights reserved                     *
*   (c) Microsoft. All rights reserved.                 *
*********************************************************/

#pragma once

#include "vgen/feature/TreeFeature.h"

namespace vgen {

class GroundBushFeature : public TreeFeature {
public:

	GroundBushFeature(int trunkType, int leafType);

	virtual bool place(BlockSource& region, const BlockPos& pos, Random& random) const override;
};

} // namespace vgen

