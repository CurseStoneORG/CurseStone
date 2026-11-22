/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| |  \__ \  __/\__/ / || (_) | | | |  __/
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
#include "NoiseOptimizer.h"

#include <cstdlib>
#include <cstring>
#include <mutex>

namespace cursestone::performance::noise {

// ---------------------------------------------------------------------------
// Scalar reference — mirrors ImprovedNoise::_readArea op for op. The
// runtime guard compares the vector kernels against this; it must stay in
// sync with the generator's own scalar loop.
// ---------------------------------------------------------------------------

static inline float calcValues(float val, int& index, float& lerp) {
    int vf = (int)val;
    if (val < vf)
        vf--;
    index = vf & 255;
    val -= vf;
    lerp = val * val * val * (val * (val * 6 - 15) + 10);
    return val;
}

static inline float grad2(int hash, float x, float z) {
    int h = hash & 15;
    float u = (1 - ((h & 8) >> 3)) * x;
    float v = h < 4 ? 0 : h == 12 || h == 14 ? x : z;
    return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
}

static inline float grad3(int hash, float px, float py, float pz) {
    int h = hash & 15;
    float u = h < 8 ? px : py;
    float v = h < 4 ? py : h == 12 || h == 14 ? px : pz;
    return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
}

static inline float lerp(float t, float a, float b) {
    return a + t * (b - a);
}

void region_scalar_2d(const Params& p) {
    const int* m = p.map;
    const float scale = 1.0f / p.pow;
    int pp = 0;
    for (int xx = 0; xx < p.xSize; xx++) {
        int X;
        float u;
        float x = (p.px + xx) * p.sx + p.ox;
        x = calcValues(x, X, u);
        for (int zz = 0; zz < p.zSize; zz++) {
            int Z;
            float w;
            float z = (p.pz + zz) * p.sz + p.oz;
            z = calcValues(z, Z, w);

            const int A = m[X];
            const int AA = m[A] + Z;
            const int B = m[X + 1];
            const int BA = m[B] + Z;
            const float vv0 = lerp(u, grad2(m[AA], x, z), grad3(m[BA], x - 1, 0, z));
            const float vv2 = lerp(u, grad3(m[AA + 1], x, 0, z - 1),
                                   grad3(m[BA + 1], x - 1, 0, z - 1));
            const float val = lerp(w, vv0, vv2);
            p.buffer[pp++] += val * scale;
        }
    }
}

void region_scalar_3d(const Params& p) {
    const int* m = p.map;
    const float scale = 1.0f / p.pow;
    int yOld = -1;
    float vv0 = 0, vv1 = 0, vv2 = 0, vv3 = 0;
    int pp = 0;
    for (int xx = 0; xx < p.xSize; xx++) {
        int X;
        float u;
        float x = (p.px + xx) * p.sx + p.ox;
        x = calcValues(x, X, u);
        for (int zz = 0; zz < p.zSize; zz++) {
            int Z;
            float w;
            float z = (p.pz + zz) * p.sz + p.oz;
            z = calcValues(z, Z, w);
            for (int yy = 0; yy < p.ySize; yy++) {
                int Y;
                float v;
                float y = (p.py + yy) * p.sy + p.oy;
                y = calcValues(y, Y, v);
                if (yy == 0 || Y != yOld) {
                    yOld = Y;
                    const int A = m[X] + Y;
                    const int AA = m[A] + Z;
                    const int AB = m[A + 1] + Z;
                    const int B = m[X + 1] + Y;
                    const int BA = m[B] + Z;
                    const int BB = m[B + 1] + Z;
                    vv0 = lerp(u, grad3(m[AA], x, y, z), grad3(m[BA], x - 1, y, z));
                    vv1 = lerp(u, grad3(m[AB], x, y - 1, z), grad3(m[BB], x - 1, y - 1, z));
                    vv2 = lerp(u, grad3(m[AA + 1], x, y, z - 1),
                               grad3(m[BA + 1], x - 1, y, z - 1));
                    vv3 = lerp(u, grad3(m[AB + 1], x, y - 1, z - 1),
                               grad3(m[BB + 1], x - 1, y - 1, z - 1));
                }
                const float v0 = lerp(v, vv0, vv1);
                const float v1 = lerp(v, vv2, vv3);
                const float val = lerp(w, v0, v1);
                p.buffer[pp++] += val * scale;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Dispatch — one-time guard, then kernel selection.
// ---------------------------------------------------------------------------
namespace {

using Kernel2D = bool (*)(const Params&);
using Kernel3D = bool (*)(const Params&);

Kernel2D k2d = nullptr;
Kernel3D k3d = nullptr;
const char* kName = "scalar";
std::once_flag kOnce;

// Reference set by ImprovedNoise (its verbatim scalar loop). Guards run
// against it when present so vector output is checked against the exact
// bits the platform's scalar path produces.
void (*gRef)(const Params&) = nullptr;

void refRegion(const Params& p) {
    if (gRef) {
        gRef(p);
        return;
    }
    if (p.ySize == 1)
        region_scalar_2d(p);
    else
        region_scalar_3d(p);
}

bool buffersEqual(const float* a, const float* b, size_t n) {
    // Bit comparison: the requirement is identical bytes, not "close".
    return memcmp(a, b, n * sizeof(float)) == 0;
}

// Synthetic permutation table; the kernels only see it as data. Values
// stay in 0..255 like the real ImprovedNoise table, where every
// m[A] + Y / m[A] + Z index lands inside 512.
void makeSyntheticMap(int map[512], float mapf[512]) {
    for (int i = 0; i < 512; ++i) {
        map[i] = (i * 37 + 11) & 255;
        mapf[i] = (float)map[i];
    }
}

bool guard2D(Kernel2D cand) {
    int map[512];
    float mapf[512];
    makeSyntheticMap(map, mapf);
    const struct { int xs, zs; float s; float pow; } cases[] = {
        {5, 5, 200.0f, 1.0f},
        {16, 16, 0.09765625f, 1.0f},
        {7, 3, 3.125e-2f, 0.125f},
        {16, 1, 684.412f, 1.0f},
        {5, 25, 1.5625e-2f, 0.03125f},
    };
    for (const auto& c : cases) {
        const size_t n = (size_t)(c.xs * c.zs);
        static float a[16 * 25], b[16 * 25];
        memset(a, 0, n * sizeof(float));
        memset(b, 0, n * sizeof(float));
        Params p{};
        p.map = map;
        p.mapf = mapf;
        p.ox = 13.37f; p.oy = 7.77f; p.oz = 129.25f;
        p.px = 96.5f; p.py = 10.0f; p.pz = -41.25f;
        p.xSize = c.xs; p.ySize = 1; p.zSize = c.zs;
        p.sx = c.s; p.sy = 1.0f; p.sz = c.s * 1.01f;
        p.pow = c.pow;
        p.buffer = a;
        refRegion(p);
        p.buffer = b;
        if (!cand(p))
            return false;
        if (!buffersEqual(a, b, n))
            return false;
    }
    return true;
}

bool guard3D(Kernel3D cand) {
    int map[512];
    float mapf[512];
    makeSyntheticMap(map, mapf);
    // sy values: the heavy terrain octaves (sy >= 1) plus the exact
    // boundary sy == 1. sy < 1 must be REJECTED by the kernel, never
    // compared (different semantics).
    const struct { int xs, ys, zs; float sy; float pow; } cases[] = {
        {5, 17, 5, 684.412f, 1.0f},
        {5, 17, 5, 855.515f, 1.0f},
        {5, 17, 5, 1.3365938f, 1.0f},
        {5, 17, 5, 1.0f, 0.125f},
        {5, 9, 5, 1.0f, 1.0f},
        {4, 24, 6, 2.0f, 0.5f},
    };
    for (const auto& c : cases) {
        const size_t n = (size_t)(c.xs * c.ys * c.zs);
        static float a[8 * 24 * 8], b[8 * 24 * 8];
        memset(a, 0, n * sizeof(float));
        memset(b, 0, n * sizeof(float));
        Params p{};
        p.map = map;
        p.mapf = mapf;
        p.ox = 55.125f; p.oy = -17.5f; p.oz = 200.0625f;
        p.px = -88.75f; p.py = 0.0f; p.pz = 41.5f;
        p.xSize = c.xs; p.ySize = c.ys; p.zSize = c.zs;
        p.sx = 684.412f; p.sy = c.sy; p.sz = 684.412f;
        p.pow = c.pow;
        p.buffer = a;
        refRegion(p);
        p.buffer = b;
        if (!cand(p))
            return false;
        if (!buffersEqual(a, b, n))
            return false;
    }
    // Note: the sy < 1 rejection is enforced by the public readArea3D
    // dispatcher, not by the kernel itself — nothing to test here.
    return true;
}

void selectKernels() {
    // Testing escape hatch: forces the scalar path so equivalence checks
    // can compare vector vs scalar output with the same binary.
    if (std::getenv("CURSESTONE_NOISE_SCALAR") != nullptr)
        return;
#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
    if (guard2D(&readArea2D_avx2))
        k2d = &readArea2D_avx2;
    if (guard3D(&readArea3D_avx2))
        k3d = &readArea3D_avx2;
    if (k2d || k3d)
        kName = "avx2";
#elif defined(__aarch64__)
    if (guard2D(&readArea2D_neon))
        k2d = &readArea2D_neon;
    if (guard3D(&readArea3D_neon))
        k3d = &readArea3D_neon;
    if (k2d || k3d)
        kName = "neon";
#endif
}

} // namespace

bool setScalarReference(void (*fn)(const Params&)) {
    if (gRef)
        return false;
    gRef = fn;
    return true;
}

const char* initialize() {
    std::call_once(kOnce, selectKernels);
    return kName;
}

bool readArea2D(const Params& p) {
    return k2d && k2d(p);
}

bool readArea3D(const Params& p) {
    if (p.sy < 1.0f)
        return false; // order-sensitive corner reuse — scalar only
    return k3d && k3d(p);
}

} // namespace cursestone::performance::noise
