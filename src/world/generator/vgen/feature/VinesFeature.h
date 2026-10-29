/********************************************************
*   (c) Mojang. All rights reserved                     *
*   (c) Microsoft. All rights reserved.                 *
*********************************************************/

#pragma once

#include "vgen/feature/Feature.h"

namespace vgen {

class VinesFeature : public Feature {
public:

	virtual bool place(BlockSource& region, const BlockPos& pos, Random& random) const override;
};

} // namespace vgen

