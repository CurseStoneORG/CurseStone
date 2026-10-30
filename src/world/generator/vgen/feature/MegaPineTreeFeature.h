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

#include "vgen/feature/MegaTreeFeature.h"

namespace vgen {

/**
 * Same as tree feature, but slightly taller and white in color.
 */
class MegaPineTreeFeature : public MegaTreeFeature {
public:

	MegaPineTreeFeature(Entity* placer, bool isSpruce);
	virtual bool place(BlockSource& region, const BlockPos& pos, Random& random) const override;

	void createCrown(BlockSource& region, const BlockPos& origin, int baseRadius, Random& random) const;
	void postPlaceTree(BlockSource& region, Random& random, const BlockPos& pos) const;

private:

	void _placePodzolCircle(BlockSource& region, const BlockPos& pos) const;
	void _placePodzolAt(BlockSource& region, const BlockPos& pos) const;

	bool mIsSpruce;
};

} // namespace vgen

