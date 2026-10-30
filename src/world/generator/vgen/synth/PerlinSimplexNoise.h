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

#include "vgen/Random.h"

namespace vgen {

class SimplexNoise;
class Vec3;

class PerlinSimplexNoise {
public:

	PerlinSimplexNoise(RandomSeed seed, int levels);
	PerlinSimplexNoise(Random& r, int levels);
	~PerlinSimplexNoise();

	float getValue(float x, float y) const;
	float getValue(const Vec3& pos) const;

	float* getRegion(float* buffer, const Vec3& pos, int xSize, int ySize, int zSize, const Vec3& scale) const;
	float* getRegion(float* buffer, const Vec2& pos, int xSize, int zSize, const Vec2& scale, float sizeScale, float powScale = 0.5f) const;

private:

	void _init(Random& r);

	const int mLevels;
	std::vector<SimplexNoise> mNoiseLevels;
};

} // namespace vgen

