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
class MegaJungleTreeFeature : public MegaTreeFeature {
public:

	MegaJungleTreeFeature(Entity* placer, int baseHeight, int heightInterval, int trunkType, int leafType);
	virtual bool place(BlockSource& region, const BlockPos& pos, Random& random) const override;

private:

	void _placeVines(BlockSource& region, const BlockPos& pos, Random& random, int height, int dirx, int dirz) const;
	void _placeVine(BlockSource& region, const BlockPos& pos, Random& random, int direction) const;
	void _createCrown(BlockSource& region, const BlockPos& pos, int baseRadius) const;
};

} // namespace vgen

