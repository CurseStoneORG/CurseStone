/********************************************************
*   (c) Mojang. All rights reserved                     *
*   (c) Microsoft. All rights reserved.                 *
*********************************************************/

#pragma once

#include "vgen/feature/TreeFeature.h"

namespace vgen {

class JungleTreeFeature : public TreeFeature {
public:

	JungleTreeFeature(Entity* placer = nullptr);
	virtual bool place(BlockSource& region, const BlockPos& pos, Random& random) const override;
};

} // namespace vgen

