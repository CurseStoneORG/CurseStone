/********************************************************
*   (c) Mojang. All rights reserved                     *
*   (c) Microsoft. All rights reserved.                 *
*********************************************************/

#pragma once

#include "vgen/feature/TreeFeature.h"
#include "vgen/Blocks.h"
#include "vgen/Blocks.h"
#include "vgen/Random.h"

namespace vgen {

class OakFeature : public TreeFeature {
public:

	OakFeature(Entity* placer = nullptr)
		: TreeFeature(placer, enum_cast(OldLogBlock::LogType::Oak), LeafBlock::NORMAL_LEAF) {

	}

	virtual bool place(BlockSource& region, const BlockPos& pos, Random& random) const override {
		int treeHeight = random.nextInt(3) + 4;
		return TreeFeature::place(region, pos, random, treeHeight);
	}
};

} // namespace vgen

