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

namespace vgen {

//	@Note: It should be noted that the order of this matters when registering the material types.
enum class MaterialType : unsigned int {
	Air,
	Dirt,
	Wood,
	Stone,
	Metal,
	Water,
	Lava,
	Leaves,
	Plant,
	ReplaceablePlant,
	Sponge,
	Cloth,
	Bed,
	Fire,
	Sand,
	Decoration,
	Glass,
	Explosive,
	Ice,
	PackedIce,
	TopSnow,
	Snow,
	Cactus,
	Clay,
	Vegetable,
	Portal,
	Cake,
	Web,
	RedstoneWire,
	Carpet,
	BuildableGlass,
	Slime,
	Piston,
	Allow,
	Deny,

	SurfaceTypeTotal,
	Any
};


} // namespace vgen

