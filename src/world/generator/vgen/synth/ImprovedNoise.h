/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

 * CurseStone | Open Source Server Software
 *
 * Copyright (C) 2026 CurseStoneORG
 *
 * Developed by CurseStoneORG.
 * Built for an open and extensible Minecraft server ecosystem.
 *
 * Licensed under the GNU Lesser General Public License v3.0 or later.
 * You may study, modify, and redistribute this software under its license.
 *
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 * Repository: https://github.com/CurseStoneORG/CurseStone
 *
 */

#pragma once
#include "vgen/Vanilla.h"
#include "world/performance/NoiseOptimizer.h"

namespace vgen {

class Random;

class ImprovedNoise {
public:

        ImprovedNoise();
        ImprovedNoise(Random& random);

        void _init(Random& random);

        float _getValue(const Vec3& pos) const;
        void _readArea(float* buffer, const Vec3& pos, int xSize, int ySize, int zSize, const Vec3& s, float pow) const;
        // The verbatim scalar region loop; also the vector kernels' guard
        // reference (registered before main in ImprovedNoise.cpp).
        static void readAreaScalar(const cursestone::performance::noise::Params& p);
        int _hashCode() const;

private:

        static float _lerp(float t, float a, float b);
        static float _grad2(int hash, float x, float z);
        static float _grad(int hash, const Vec3& p);
        static float _calcValues(float val, int& index, float& lerp);

        float _noise(const Vec3& pos) const;
        static void _blendCubeCorners(const int* map, const Vec3& origin, int X, int Y, int Z, float u, float& vv0, float& vv1, float& vv2, float& vv3);

        Vec3 mOrigin;
        int mNoiseMap[512];
        // Float copy of the permutation table for the vector kernels'
        // gather loads (values are exact small integers, so gather + truncate
        // reproduces mNoiseMap[] exactly).
        float mNoiseMapF[512];
};

} // namespace vgen

