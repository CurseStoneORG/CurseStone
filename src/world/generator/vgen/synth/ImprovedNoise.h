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

class Random;

class ImprovedNoise {
public:

	ImprovedNoise();
	ImprovedNoise(Random& random);

	void _init(Random& random);

	float _getValue(const Vec3& pos) const;
	void _readArea(float* buffer, const Vec3& pos, int xSize, int ySize, int zSize, const Vec3& s, float pow) const;
	int _hashCode() const;

private:

	static float _lerp(float t, float a, float b);
	static float _grad2(int hash, float x, float z);
	static float _grad(int hash, const Vec3& p);
	static float _calcValues(float val, int& index, float& lerp);

	float _noise(const Vec3& pos) const;
	void _blendCubeCorners(const Vec3& origin, int X, int Y, int Z, float u, float& vv0, float& vv1, float& vv2, float& vv3) const;

	Vec3 mOrigin;
	int mNoiseMap[512];
};

} // namespace vgen

