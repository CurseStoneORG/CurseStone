/********************************************************
*   (c) Mojang. All rights reserved                     *
*   (c) Microsoft. All rights reserved.                 *
*********************************************************/

#pragma once

#include "vgen/feature/Feature.h"

namespace vgen {

enum class EntityType;

class MonsterRoomFeature : public Feature {

public:

	MonsterRoomFeature();

	virtual ~MonsterRoomFeature() {
	}

	virtual bool place(BlockSource& region, const BlockPos& pos, Random& random) const override;

private:

	EntityType _getRandomEntityId(Random& random) const;
	DataID _getFacingDataId(BlockSource& region, const BlockPos & blockPos, int offsetX, int offsetZ) const;
};

} // namespace vgen

