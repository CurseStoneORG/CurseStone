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

#include "vgen/synth/ImprovedNoise.h"
#include "world/performance/NoiseOptimizer.h"
#include "vgen/Random.h"

namespace vgen {

ImprovedNoise::ImprovedNoise() {
	Random random(1);
	_init(random);
}

ImprovedNoise::ImprovedNoise(Random& random) {
	_init(random);
}

void ImprovedNoise::_init(Random& random) {
	mOrigin.x = random.nextFloat() * 256.f;
	mOrigin.y = random.nextFloat() * 256.f;
	mOrigin.z = random.nextFloat() * 256.f;

	for (int i = 0; i < 256; i++) {
		mNoiseMap[i] = i;
	}

	for (int i = 0; i < 256; i++) {
		int j = random.nextInt(256 - i) + i;
		int tmp = mNoiseMap[i];
		mNoiseMap[i] = mNoiseMap[j];
		mNoiseMap[j] = tmp;

		mNoiseMap[i + 256] = mNoiseMap[i];
	}
}

float ImprovedNoise::_noise(const Vec3& pos) const {
	int X, Y, Z;
	float u, v, w;

	Vec3 p = pos + mOrigin;
	p.x = _calcValues(p.x, X, u);
	p.y = _calcValues(p.y, Y, v);
	p.z = _calcValues(p.z, Z, w);

	float vv0 = 0, vv1 = 0, vv2 = 0, vv3 = 0;
	_blendCubeCorners(mNoiseMap, p, X, Y, Z, u, vv0, vv1, vv2, vv3);

	// ADD BLENDED RESULTS FROM 8 CORNERS OF CUBE
	return _lerp(w, _lerp(v, vv0, vv1), _lerp(v, vv2, vv3));
}

float ImprovedNoise::_lerp(float t, float a, float b) {
	return a + t * (b - a);
}

float ImprovedNoise::_grad2(int hash, float x, float z) {
	int h = hash & 15;	// CONVERT LO 4 BITS OF HASH CODE...

	// ...INTO 12 GRADIENT DIRECTIONS.
	float u = (1 - ((h & 8) >> 3)) * x;
	float v = h < 4 ? 0 : h == 12 || h == 14 ? x : z;

	return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
}

float ImprovedNoise::_grad(int hash, const Vec3& p) {
	int h = hash & 15;	// CONVERT LO 4 BITS OF HASH CODE...

	// ...INTO 12 GRADIENT DIRECTIONS.
	float u = h < 8 ? p.x : p.y;
	float v = h < 4 ? p.y : h == 12 || h == 14 ? p.x : p.z;

	return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
}

float ImprovedNoise::_calcValues(float val, int& index, float& lerp) {
	int vf = (int)val;
	if (val < vf) {
		vf--;
	}
	index = vf & 255;	// FIND UNIT THAT CONTAINS POINT.
	val -= vf;	// FIND RELATIVE COORD OF POINT IN CUBE.
	lerp = val * val * val * (val * (val * 6 - 15) + 10);	// COMPUTE FADE CURVES FOR COORD.

	return val;
}

float ImprovedNoise::_getValue(const Vec3& pos) const {
	return _noise(pos);
}

void ImprovedNoise::_readArea(float* buffer, const Vec3& pos, int xSize, int ySize, int zSize, const Vec3& s, float pow) const {
        // Vector kernels first. The runtime guard in NoiseOptimizer
        // compares them against THIS file's scalar loop (registered
        // below), so a platform whose compiler fuses scalar expressions
        // (e.g. fmadd on ARM64) automatically keeps its established
        // scalar output instead of switching to different kernel bits.
        cursestone::performance::noise::Params prm;
        prm.map = mNoiseMap;
        prm.mapf = mNoiseMapF;
        prm.ox = mOrigin.x; prm.oy = mOrigin.y; prm.oz = mOrigin.z;
        prm.px = pos.x; prm.py = pos.y; prm.pz = pos.z;
        prm.xSize = xSize; prm.ySize = ySize; prm.zSize = zSize;
        prm.sx = s.x; prm.sy = s.y; prm.sz = s.z;
        prm.pow = pow;
        prm.buffer = buffer;
        if (ySize == 1) {
                if (cursestone::performance::noise::readArea2D(prm))
                        return;
        } else if (cursestone::performance::noise::readArea3D(prm)) {
                return;
        }
        readAreaScalar(prm);
}

// The verbatim scalar loop, also the vector kernels' guard reference.
// Params carries everything this needs (permutation table, origin,
// region origin/scale/amplitude, output buffer).
void ImprovedNoise::readAreaScalar(const cursestone::performance::noise::Params& p) {
        const float* posP = &p.px;
        (void)posP;
        const float ox = p.ox, oy = p.oy, oz = p.oz;
        const float sx = p.sx, sy = p.sy, sz = p.sz;
        const float pow = p.pow;
        const int xSize = p.xSize, ySize = p.ySize, zSize = p.zSize;
        float* buffer = p.buffer;
        const int* mNoiseMap = p.map;
        const float posx = p.px, posy = p.py, posz = p.pz;
        (void)posy;

        if (ySize == 1) {
                int A = 0, AA = 0, B = 0, BA = 0;
                float vv0 = 0, vv2 = 0;
                int pp = 0;
                float scale = 1.0f / pow;

                for (int xx = 0; xx < xSize; xx++) {
                        int X;
                        float u;
                        float x = (posx + xx) * sx + ox;
                        x = _calcValues(x, X, u);

                        for (int zz = 0; zz < zSize; zz++) {
                                int Z;
                                float w;
                                float z = (posz + zz) * sz + oz;
                                z = _calcValues(z, Z, w);

                                A = mNoiseMap[X] + 0;
                                AA = mNoiseMap[A] + Z;
                                B = mNoiseMap[X + 1] + 0;
                                BA = mNoiseMap[B] + Z;
                                vv0 = _lerp(u, _grad2(mNoiseMap[AA], x, z), _grad(mNoiseMap[BA], Vec3(x - 1, 0, z)));
                                vv2 = _lerp(u, _grad(mNoiseMap[AA + 1], Vec3(x, 0, z - 1)), _grad(mNoiseMap[BA + 1], Vec3(x - 1, 0, z - 1)));

                                float val = _lerp(w, vv0, vv2);
                                buffer[pp++] += val * scale;
                        }
                }
                return;
        }

        int pp = 0;
        float scale = 1 / pow;
        int yOld = -1;
        float vv0 = 0, vv1 = 0, vv2 = 0, vv3 = 0;

        for (int xx = 0; xx < xSize; xx++) {
                int X;
                float u;
                float x = (posx + xx) * sx + ox;
                x = _calcValues(x, X, u);

                for (int zz = 0; zz < zSize; zz++) {
                        int Z;
                        float w;
                        float z = (posz + zz) * sz + oz;
                        z = _calcValues(z, Z, w);

                        for (int yy = 0; yy < ySize; yy++) {
                                int Y;
                                float v;
                                float y = (posy + yy) * sy + oy;
                                y = _calcValues(y, Y, v);

                                if (yy == 0 || Y != yOld) {
                                        yOld = Y;
                                        _blendCubeCorners(p.map, Vec3(x, y, z), X, Y, Z, u, vv0, vv1, vv2, vv3);
                                }

                                float v0 = _lerp(v, vv0, vv1);
                                float v1 = _lerp(v, vv2, vv3);
                                float val = _lerp(w, v0, v1);

                                buffer[pp++] += val * scale;
                        }
                }
        }
}

// Registers the scalar loop as the kernels' guard reference before main()
// runs, so the guard compares vector output against the platform's actual
// scalar bits (including any compiler fusion in the scalar expressions).
namespace {
const bool g_vgen_noise_ref_registered =
    cursestone::performance::noise::setScalarReference(&vgen::ImprovedNoise::readAreaScalar);
}

int ImprovedNoise::_hashCode() const {
	int x = 4711;

	for (int i = 0; i < 512; ++i) {
		x = x * 37 + mNoiseMap[i];
	}

	return x;
}

void ImprovedNoise::_blendCubeCorners(const int* map, const Vec3& origin, int X, int Y, int Z, float u, float& vv0, float& vv1, float& vv2, float& vv3) {
	int A = map[X] + Y;
	int AA = map[A] + Z;
	int AB = map[A + 1] + Z;
	int B = map[X + 1] + Y;
	int BA = map[B] + Z;		// HASH COORDINATES OF
	int BB = map[B + 1] + Z;	// THE 8 CUBE CORNERS

	Vec3 ul = origin - Vec3(1, 0, 0);
	Vec3 br = origin - Vec3(0, 1, 0);
	Vec3 bl = origin - Vec3(1, 1, 0);
	Vec3 fur = origin - Vec3(0, 0, 1);
	Vec3 ful = origin - Vec3(1, 0, 1);
	Vec3 fbr = origin - Vec3(0, 1, 1);
	Vec3 fbl = origin - Vec3(1, 1, 1);

	// BLEND RESULTS FROM 8 CORNERS OF CUBE
	vv0 = _lerp(u, _grad(map[AA], origin), _grad(map[BA], ul));
	vv1 = _lerp(u, _grad(map[AB], br), _grad(map[BB], bl));
	vv2 = _lerp(u, _grad(map[AA + 1], fur), _grad(map[BA + 1], ful));
	vv3 = _lerp(u, _grad(map[AB + 1], fbr), _grad(map[BB + 1], fbl));
}

} // namespace vgen

