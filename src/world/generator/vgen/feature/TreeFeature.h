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

#include "vgen/feature/Feature.h"

namespace vgen {

class Material;

class TreeFeature : public Feature {
public:

	TreeFeature(Entity* placer, int trunkType, int leafType, bool addJungleFeatures = false);
	virtual bool place(BlockSource& region, const BlockPos& pos, Random& random) const override;
	bool place(BlockSource& region, const BlockPos& pos, Random& random, int treeHeight) const;

protected:

	int _getTrunkType() const;
	int _getLeafType() const;

	void _placeLeaf(BlockSource& region, const BlockPos& pos) const;
	bool _placeTrunk(BlockSource& region, const BlockPos& pos, Random& random, int treeHeight) const;

	bool _prepareSpawn(BlockSource& region, const BlockPos& pos, int treeHeight) const;

	bool _isFree(BlockID block) const;
	bool _isFree(const Material& material) const;

private:

	void _placeFallenTrunk(BlockSource& region, const BlockPos& pos, Random& random, int treeHeight) const;
	void _addVine(BlockSource& region, const BlockPos& pos, int dir) const;
	void _addJungleFeatures(BlockSource& region, const BlockPos& vinePos, Random& random) const;
	void _addCocoaPlants(BlockSource& region, const BlockPos& pos, Random& random, int treeHeight) const;

	int mTrunkType;
	int mLeafType;
	bool mAddJungleFeatures;

};

} // namespace vgen

