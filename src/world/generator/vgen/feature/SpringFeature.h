/********************************************************
*   (c) Mojang. All rights reserved                     *
*   (c) Microsoft. All rights reserved.                 *
*********************************************************/

#pragma once

#include "vgen/feature/Feature.h"

namespace vgen {

class SpringFeature : public Feature {
public:

	SpringFeature(BlockID block);
	virtual bool place(BlockSource& region, const BlockPos& pos, Random& random) const override;

private:

	BlockID mBlock;
};

} // namespace vgen

