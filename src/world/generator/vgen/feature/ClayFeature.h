/********************************************************
*   (c) Mojang. All rights reserved                     *
*   (c) Microsoft. All rights reserved.                 *
*********************************************************/

#pragma once

#include "vgen/feature/Feature.h"

namespace vgen {

class ClayFeature : public Feature {

public:

	ClayFeature(int radius);
	virtual bool place(BlockSource& region, const BlockPos& pos, Random& random) const override;

private:

	const int mRadius;
};

} // namespace vgen

