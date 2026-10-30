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

