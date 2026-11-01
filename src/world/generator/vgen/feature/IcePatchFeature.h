/********************************************************
*   (c) Mojang. All rights reserved                     *
*   (c) Microsoft. All rights reserved.                 *
*********************************************************/

#pragma once

#include "vgen/feature/Feature.h"

namespace vgen {

class IcePatchFeature : public Feature {
public:

	IcePatchFeature(int radius);
	virtual bool place(BlockSource& region, const BlockPos& pos, Random& random) const override;

private:

	FullBlock mBlock;
	int mRadius;
};

} // namespace vgen

