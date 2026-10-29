/********************************************************
*  CurseStone Server — vanilla terrain generator port   *
*         (c) CurseStone. All rights reserved.          *
*********************************************************/

#pragma once

#include "vgen/Vanilla.h"

namespace vgen {

class BlockPos;
class BlockSource;
class Random;
class Entity;

class Feature {
public:

	Feature(Entity* placer = nullptr);

	virtual ~Feature() {
	}

	virtual bool place(BlockSource& region, const BlockPos& pos, Random& random) const = 0;

protected:
	void _setManuallyPlaced(Entity* placer);
	bool _getManuallyPlaced() const;
	bool _placeBlock(BlockSource& region, const BlockPos& pos, const FullBlock& blockData) const;
	bool _setBlockAndData(BlockSource& region, const BlockPos& pos, const FullBlock& blockData) const;

private:

	Entity* mPlacer;
};

} // namespace vgen

