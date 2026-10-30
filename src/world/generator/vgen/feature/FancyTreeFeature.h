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

class FancyTreeFeature : public TreeFeature {
protected:

	class FoliageCoords : public BlockPos {
	public:

		FoliageCoords(BlockPos pos, int branchBase);
		int getBranchBase() const;

	private:
		int mBranchBase;
	};

public:

	FancyTreeFeature(Entity* placer = nullptr);
	virtual bool place(BlockSource& region, const BlockPos& pos, Random& random) const override;

protected:

	float _getTreeShape(int height, int y) const;
	bool _checkLocation(BlockSource& region, const BlockPos& origin, int& height) const;
	int _checkLine(BlockSource& region, const BlockPos& startPos, const BlockPos& endPos) const;
	int _getSteps(const BlockPos& pos) const;
	void _makeFoliage(BlockSource& region, const std::vector<FoliageCoords>& foliageCoords) const;
	void _foliageCluster(BlockSource& region, const FoliageCoords& foliageCoord) const;
	float _getFoliageShape(int y) const;
	void _crossection(BlockSource& region, const BlockPos& pos, float radius, const BlockID block) const;
	void _makeTrunk(BlockSource& region, const BlockPos& origin, int trunkHeight) const;
	void _limb(BlockSource& region, const BlockPos& startPos, const BlockPos& endPos, const BlockID material) const;
	int _getLogFacingData(const BlockPos& startPos, const BlockPos& blockPos) const;
	void _makeBranches(BlockSource& region, const std::vector<FoliageCoords>& foliageCoords, const BlockPos& origin, int height) const;
	bool _trimBranches(int height, int localY) const;

private:

	const int mHeightVariance = 12;
	const float mTrunkHeightScale = 0.618f;
	const float mBranchSlope = 0.381f;
	const float mWidthScale = 1;
	const float mFoliageDensity = 1;
	const int mTrunkWidth = 1;
	const int mFoliageHeight = 4;
	const int mMinAllowedHeight = 6;
};

} // namespace vgen

