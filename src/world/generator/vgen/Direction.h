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

class Direction {
public:
	enum Type : unsigned char {
		SOUTH = 0,
		WEST,
		NORTH,
		EAST,
		UNDEFINED = 0xFF
	};

	// for [direction] it gives [block-face]
	static const FacingID DIRECTION_FACING[4];

	// for a given face gives direction
	static const int FACING_DIRECTION[6];

	// for [direction] it gives [opposite direction]
	static const int DIRECTION_OPPOSITE[4];

	// for [direction][world-facing] it gives [block-facing]
	static const FacingID RELATIVE_DIRECTION_FACING[4][6];

	// for [direction] it gives [90 degrees clockwise direction]
	static const int DIRECTION_CLOCKWISE[4];

	// for [direction] it gives [90 degrees counter-clockwise direction]
	static const int DIRECTION_COUNTER_CLOCKWISE[4];

	static const int STEP_X[4];
	static const int STEP_Z[4];

	static int getDirection(float xd, float zd);
	static int getDirection(int x0, int z0, int x1, int z1);

};

} // namespace vgen

