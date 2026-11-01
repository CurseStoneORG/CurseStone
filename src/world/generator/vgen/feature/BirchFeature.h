/********************************************************
*   (c) Mojang. All rights reserved                     *
*   (c) Microsoft. All rights reserved.                 *
*********************************************************/

#pragma once

#include "vgen/feature/TreeFeature.h"

namespace vgen {

/**
 * Same as tree feature, but slightly taller and white in color.
 */
class BirchFeature : public TreeFeature {
public:

	BirchFeature(Entity* placer = nullptr, bool superBirch = false);

	virtual bool place(BlockSource& region, const BlockPos& pos, Random& random) const override;

protected:

	bool mSuperBirch;
};

} // namespace vgen

