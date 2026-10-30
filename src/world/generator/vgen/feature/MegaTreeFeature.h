/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

 * CurseStone | Open Source Server Software
 *
 * Copyright (C) 2026 CurseStoneTeam
 *
 * Developed by CurseStoneTeam.
 * Built for an open and extensible Minecraft server ecosystem.
 *
 * Licensed under the GNU Lesser General Public License v3.0 or later.
 * You may study, modify, and redistribute this software under its license.
 *
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 * Repository: https://github.com/CurseStoneTeam/CurseStone
 *
 */

#pragma once

#include "vgen/feature/TreeFeature.h"
#include "vgen/Vanilla.h"

namespace vgen {

/**
 * Same as tree feature, but slightly taller and white in color.
 */
class MegaTreeFeature : public TreeFeature {
public:

	MegaTreeFeature(Entity* placer, int baseHeight, int heightInterval, int trunkType, int leafType);
	virtual bool place(BlockSource& region, const BlockPos& pos, Random& random) const override;

protected:

	int _calcTreeHeigth(Random& random) const;
	bool _checkIsFree(BlockSource& region, const BlockPos& pos, int treeHeight) const;
	bool _makeDirtFloor(BlockSource& region, const BlockPos& pos) const;
	bool _prepareTree(BlockSource& region, const BlockPos& pos, int treeHeight) const;
	void _placeDoubleTrunkLeaves(BlockSource& region, const BlockPos& origin, int radius) const;
	void _placeSingleTrunkLeaves(BlockSource& region, const BlockPos& origin, int radius) const;

	int _getBaseHeight() const;
	int _getHeightInterval() const;
	int _getTrunkType() const;
	int _getLeafType() const;

private:

	const int mBaseHeight;
	const int mHeightInterval;
	const int mTrunkType;
	const int mLeafType;
};

} // namespace vgen

