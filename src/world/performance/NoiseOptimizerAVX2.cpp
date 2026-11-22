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
// AVX2 kernels for the vanilla Perlin octave regions. Bit-exactness
// rules for this file: only exact IEEE ops (mul/sub/add/div/cmp), no
// FMA intrinsics, no reassociation, same op order as the scalar loop.
// Compiled with -mavx2 -mno-fma (GCC/Clang) or /arch:AVX2 (MSVC), which
// never introduces fused ops.
//
// Conventions per lane:
//   _calcValues: cvttps2dq truncates like the scalar (int)val cast, the
//   "val < trunc" fixup reproduces the floor, index & 255, and the fade
//   polynomial keeps the scalar association order.
#include "NoiseOptimizer.h"

#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
#include <immintrin.h>

namespace cursestone::performance::noise {
namespace {

constexpr int kLanes = 8;

// (int)val with the val<vf fixup, exactly like ImprovedNoise::_calcValues.
static inline __m256 calcValues(__m256 val, __m256i& index, __m256& fade) {
    const __m256i trunc = _mm256_cvttps_epi32(val);
    const __m256i fixup = _mm256_and_si256(
        _mm256_castps_si256(_mm256_cmp_ps(val, _mm256_cvtepi32_ps(trunc), _CMP_LT_OQ)),
        _mm256_set1_epi32(-1));
    const __m256i vf = _mm256_add_epi32(trunc, fixup);
    index = _mm256_and_si256(vf, _mm256_set1_epi32(255));
    const __m256 frac = _mm256_sub_ps(val, _mm256_cvtepi32_ps(vf));
    // val*val*val*(val*(val*6-15)+10), scalar association order.
    const __m256 y2 = _mm256_mul_ps(frac, frac);
    const __m256 y3 = _mm256_mul_ps(y2, frac);
    const __m256 t6 = _mm256_mul_ps(frac, _mm256_set1_ps(6.0f));
    const __m256 t15 = _mm256_sub_ps(t6, _mm256_set1_ps(15.0f));
    const __m256 t10 = _mm256_add_ps(_mm256_mul_ps(frac, t15), _mm256_set1_ps(10.0f));
    fade = _mm256_mul_ps(y3, t10);
    return frac;
}

static inline __m256 lerp(__m256 t, __m256 a, __m256 b) {
    return _mm256_add_ps(a, _mm256_mul_ps(t, _mm256_sub_ps(b, a)));
}

// Accumulates val*scale into buffer[pp..pp+7]; all lanes valid.
static inline void accumulate(__m256 val, float scale, float* buf) {
    const __m256 cur = _mm256_loadu_ps(buf);
    _mm256_storeu_ps(buf, _mm256_add_ps(cur, _mm256_mul_ps(val, _mm256_set1_ps(scale))));
}

// _grad(hash, px, py, pz) with per-lane hash and coordinates. The hash
// arrives as the gathered table value (exact small integer as float).
static inline __m256 grad(__m256 hashF, __m256 px, __m256 py, __m256 pz) {
    const __m256i hash = _mm256_cvttps_epi32(hashF);
    const __m256i h = _mm256_and_si256(hash, _mm256_set1_epi32(15));
    const __m256 hps = _mm256_cvtepi32_ps(h);
    // u = h < 8 ? px : py
    const __m256 u = _mm256_blendv_ps(py, px, _mm256_cmp_ps(hps, _mm256_set1_ps(8.0f), _CMP_LT_OQ));
    // v = h < 4 ? py : (h == 12 || h == 14 ? px : pz)
    const __m256i eq12 = _mm256_cmpeq_epi32(h, _mm256_set1_epi32(12));
    const __m256i eq14 = _mm256_cmpeq_epi32(h, _mm256_set1_epi32(14));
    const __m256 vInner = _mm256_blendv_ps(
        pz, px, _mm256_castsi256_ps(_mm256_or_si256(eq12, eq14)));
    const __m256 v = _mm256_blendv_ps(vInner, py, _mm256_cmp_ps(hps, _mm256_set1_ps(4.0f), _CMP_LT_OQ));
    // sign flips: ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v)
    const __m256i b1 = _mm256_and_si256(h, _mm256_set1_epi32(1));
    const __m256i b2 = _mm256_and_si256(h, _mm256_set1_epi32(2));
    const __m256 neg1 = _mm256_xor_ps(u, _mm256_set1_ps(-0.0f));
    const __m256 neg2 = _mm256_xor_ps(v, _mm256_set1_ps(-0.0f));
    const __m256 r1 = _mm256_blendv_ps(neg1, u, _mm256_castsi256_ps(_mm256_cmpeq_epi32(b1, _mm256_setzero_si256())));
    const __m256 r2 = _mm256_blendv_ps(neg2, v, _mm256_castsi256_ps(_mm256_cmpeq_epi32(b2, _mm256_setzero_si256())));
    return _mm256_add_ps(r1, r2);
}

// _grad2(hash, x, z): u = (1 - ((h & 8) >> 3)) * x  ->  h & 8 ? 0 : x.
static inline __m256 grad2(__m256 hashF, __m256 x, __m256 z) {
    const __m256i hash = _mm256_cvttps_epi32(hashF);
    const __m256i h = _mm256_and_si256(hash, _mm256_set1_epi32(15));
    const __m256 hps = _mm256_cvtepi32_ps(h);
    const __m256 u = _mm256_blendv_ps(
        _mm256_setzero_ps(), x,
        _mm256_castsi256_ps(_mm256_cmpeq_epi32(_mm256_and_si256(h, _mm256_set1_epi32(8)),
                                               _mm256_setzero_si256())));
    const __m256i eq12 = _mm256_cmpeq_epi32(h, _mm256_set1_epi32(12));
    const __m256i eq14 = _mm256_cmpeq_epi32(h, _mm256_set1_epi32(14));
    const __m256 vInner = _mm256_blendv_ps(
        z, x, _mm256_castsi256_ps(_mm256_or_si256(eq12, eq14)));
    const __m256 v = _mm256_blendv_ps(vInner, _mm256_setzero_ps(),
                                      _mm256_cmp_ps(hps, _mm256_set1_ps(4.0f), _CMP_LT_OQ));
    const __m256i b1 = _mm256_and_si256(h, _mm256_set1_epi32(1));
    const __m256i b2 = _mm256_and_si256(h, _mm256_set1_epi32(2));
    const __m256 neg1 = _mm256_xor_ps(u, _mm256_set1_ps(-0.0f));
    const __m256 neg2 = _mm256_xor_ps(v, _mm256_set1_ps(-0.0f));
    const __m256 r1 = _mm256_blendv_ps(neg1, u, _mm256_castsi256_ps(_mm256_cmpeq_epi32(b1, _mm256_setzero_si256())));
    const __m256 r2 = _mm256_blendv_ps(neg2, v, _mm256_castsi256_ps(_mm256_cmpeq_epi32(b2, _mm256_setzero_si256())));
    return _mm256_add_ps(r1, r2);
}

// Safe gather: garbage lanes are clamped into the table so no lane can
// read out of bounds (results for invalid lanes are discarded).
static inline __m256 gatherMap(const float* mapf, __m256i idx) {
    const __m256i lim = _mm256_max_epi32(
        _mm256_min_epi32(idx, _mm256_set1_epi32(511)), _mm256_setzero_si256());
    return _mm256_i32gather_ps(mapf, lim, 4);
}

// Accumulates only the first `n` lanes (tail handling): garbage lanes
// were computed with clamped, in-range indices and are discarded here.
static inline void accumulateTail(__m256 val, float scale, float* buf, int n) {
    alignas(32) float tmp[kLanes];
    _mm256_store_ps(tmp, val);
    for (int i = 0; i < n; ++i)
        buf[i] += tmp[i] * scale;
}

} // namespace

bool readArea2D_avx2(const Params& p) {
    const float* mapf = p.mapf;
    if (!mapf)
        return false;
    const int total = p.xSize * p.zSize;
    const float scale = 1.0f / p.pow;

    int pp = 0;
    for (; pp + kLanes <= total; pp += kLanes) {
        // Per-lane (xx, zz) for the flat point range [pp, pp+8).
        int xs[kLanes], zs[kLanes];
        for (int i = 0; i < kLanes; ++i) {
            xs[i] = (pp + i) / p.zSize;
            zs[i] = (pp + i) % p.zSize;
        }
        const __m256i xxv = _mm256_loadu_si256((const __m256i*)xs);
        const __m256i zzv = _mm256_loadu_si256((const __m256i*)zs);
        const __m256 xf = _mm256_cvtepi32_ps(xxv);
        const __m256 zf = _mm256_cvtepi32_ps(zzv);

        __m256i X, Z;
        __m256 u, w;
        const __m256 x = calcValues(
            _mm256_add_ps(_mm256_mul_ps(_mm256_add_ps(_mm256_set1_ps(p.px), xf),
                                        _mm256_set1_ps(p.sx)),
                          _mm256_set1_ps(p.ox)),
            X, u);
        const __m256 z = calcValues(
            _mm256_add_ps(_mm256_mul_ps(_mm256_add_ps(_mm256_set1_ps(p.pz), zf),
                                        _mm256_set1_ps(p.sz)),
                          _mm256_set1_ps(p.oz)),
            Z, w);

        // A = m[X], AA = m[A] + Z, B = m[X + 1], BA = m[B] + Z.
        const __m256i Av = _mm256_cvttps_epi32(gatherMap(mapf, X));
        const __m256i Bv = _mm256_cvttps_epi32(gatherMap(mapf, _mm256_add_epi32(X, _mm256_set1_epi32(1))));
        const __m256i AAv = _mm256_add_epi32(_mm256_cvttps_epi32(gatherMap(mapf, Av)), Z);
        const __m256i BAv = _mm256_add_epi32(_mm256_cvttps_epi32(gatherMap(mapf, Bv)), Z);
        const __m256i AA1 = _mm256_add_epi32(AAv, _mm256_set1_epi32(1));
        const __m256i BA1 = _mm256_add_epi32(BAv, _mm256_set1_epi32(1));

        const __m256 gAA = gatherMap(mapf, AAv);
        const __m256 gBA = gatherMap(mapf, BAv);
        const __m256 gAA1 = gatherMap(mapf, AA1);
        const __m256 gBA1 = gatherMap(mapf, BA1);

        // vv0 = lerp(u, grad2(m[AA], x, z), grad(m[BA], x-1, 0, z))
        const __m256 vv0 = lerp(u, grad2(gAA, x, z),
                                grad(gBA, _mm256_sub_ps(x, _mm256_set1_ps(1.0f)),
                                     _mm256_setzero_ps(), z));
        // vv2 = lerp(u, grad(m[AA + 1], x, 0, z-1), grad(m[BA + 1], x-1, 0, z-1))
        const __m256 vv2 = lerp(u, grad(gAA1, x, _mm256_setzero_ps(),
                                        _mm256_sub_ps(z, _mm256_set1_ps(1.0f))),
                                grad(gBA1, _mm256_sub_ps(x, _mm256_set1_ps(1.0f)),
                                     _mm256_setzero_ps(),
                                     _mm256_sub_ps(z, _mm256_set1_ps(1.0f))));
        const __m256 val = lerp(w, vv0, vv2);
        accumulate(val, scale, p.buffer + pp);
    }
    if (pp < total) {
        // Tail: run the same vector shape with the last partial batch.
        const int n = total - pp;
        int xs[kLanes], zs[kLanes];
        for (int i = 0; i < kLanes; ++i) {
            const int idx = pp + (i < n ? i : 0);
            xs[i] = idx / p.zSize;
            zs[i] = idx % p.zSize;
        }
        const __m256i xxv = _mm256_loadu_si256((const __m256i*)xs);
        const __m256i zzv = _mm256_loadu_si256((const __m256i*)zs);
        const __m256 xf = _mm256_cvtepi32_ps(xxv);
        const __m256 zf = _mm256_cvtepi32_ps(zzv);

        __m256i X, Z;
        __m256 u, w;
        const __m256 x = calcValues(
            _mm256_add_ps(_mm256_mul_ps(_mm256_add_ps(_mm256_set1_ps(p.px), xf),
                                        _mm256_set1_ps(p.sx)),
                          _mm256_set1_ps(p.ox)),
            X, u);
        const __m256 z = calcValues(
            _mm256_add_ps(_mm256_mul_ps(_mm256_add_ps(_mm256_set1_ps(p.pz), zf),
                                        _mm256_set1_ps(p.sz)),
                          _mm256_set1_ps(p.oz)),
            Z, w);

        const __m256i Av = _mm256_cvttps_epi32(gatherMap(mapf, X));
        const __m256i Bv = _mm256_cvttps_epi32(gatherMap(mapf, _mm256_add_epi32(X, _mm256_set1_epi32(1))));
        const __m256i AAv = _mm256_add_epi32(_mm256_cvttps_epi32(gatherMap(mapf, Av)), Z);
        const __m256i BAv = _mm256_add_epi32(_mm256_cvttps_epi32(gatherMap(mapf, Bv)), Z);
        const __m256 gAA = gatherMap(mapf, AAv);
        const __m256 gBA = gatherMap(mapf, BAv);
        const __m256 gAA1 = gatherMap(mapf, _mm256_add_epi32(AAv, _mm256_set1_epi32(1)));
        const __m256 gBA1 = gatherMap(mapf, _mm256_add_epi32(BAv, _mm256_set1_epi32(1)));
        const __m256 vv0 = lerp(u, grad2(gAA, x, z),
                                grad(gBA, _mm256_sub_ps(x, _mm256_set1_ps(1.0f)),
                                     _mm256_setzero_ps(), z));
        const __m256 vv2 = lerp(u, grad(gAA1, x, _mm256_setzero_ps(),
                                        _mm256_sub_ps(z, _mm256_set1_ps(1.0f))),
                                grad(gBA1, _mm256_sub_ps(x, _mm256_set1_ps(1.0f)),
                                     _mm256_setzero_ps(),
                                     _mm256_sub_ps(z, _mm256_set1_ps(1.0f))));
        const __m256 val = lerp(w, vv0, vv2);
        accumulateTail(val, scale, p.buffer + pp, n);
    }
    return true;
}

bool readArea3D_avx2(const Params& p) {
    const float* mapf = p.mapf;
    if (!mapf || p.ySize <= 1)
        return false;
    const float scale = 1.0f / p.pow;
    const __m256 scalev = _mm256_set1_ps(scale);
    const int* m = p.map;

    for (int xx = 0; xx < p.xSize; xx++) {
        int X, Z;
        float u, w;
        float x = (p.px + xx) * p.sx + p.ox;
        {
            // Scalar _calcValues for the column-fixed axes.
            int vf = (int)x;
            if (x < vf)
                vf--;
            X = vf & 255;
            x -= vf;
            u = x * x * x * (x * (x * 6 - 15) + 10);
        }
        for (int zz = 0; zz < p.zSize; zz++) {
            float zc = (p.pz + zz) * p.sz + p.oz;
            int vfz = (int)zc;
            if (zc < vfz)
                vfz--;
            Z = vfz & 255;
            zc -= vfz;
            w = zc * zc * zc * (zc * (zc * 6 - 15) + 10);

            // Column-fixed table entries (scalar, matches scalar loop).
            const int mxX = m[X];
            const int mxX1 = m[X + 1];
            const __m256i Abase = _mm256_set1_epi32(mxX);
            const __m256i Bbase = _mm256_set1_epi32(mxX1);
            const __m256i Zv = _mm256_set1_epi32(Z);
            const __m256 xb = _mm256_set1_ps(x);
            const __m256 xb1 = _mm256_set1_ps(x - 1.0f);
            const __m256 zb = _mm256_set1_ps(zc);
            const __m256 zb1 = _mm256_set1_ps(zc - 1.0f);
            const __m256 ub = _mm256_set1_ps(u);
            const __m256 wb = _mm256_set1_ps(w);

            float* col = p.buffer + (xx * p.zSize + zz) * p.ySize;
            int yy = 0;
            for (; yy + kLanes <= p.ySize; yy += kLanes) {
                // y-lane coordinates
                const __m256 yIdx = _mm256_cvtepi32_ps(
                    _mm256_add_epi32(_mm256_set1_epi32(yy), _mm256_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7)));
                __m256i Y;
                __m256 v;
                const __m256 y = calcValues(
                    _mm256_add_ps(_mm256_mul_ps(_mm256_add_ps(_mm256_set1_ps(p.py), yIdx),
                                                _mm256_set1_ps(p.sy)),
                                  _mm256_set1_ps(p.oy)),
                    Y, v);

                const __m256i Av = _mm256_add_epi32(Abase, Y);
                const __m256i Bv = _mm256_add_epi32(Bbase, Y);
                const __m256i AAv = _mm256_add_epi32(_mm256_cvttps_epi32(gatherMap(mapf, Av)), Zv);
                const __m256i ABv = _mm256_add_epi32(_mm256_cvttps_epi32(gatherMap(mapf, _mm256_add_epi32(Av, _mm256_set1_epi32(1)))), Zv);
                const __m256i BAv = _mm256_add_epi32(_mm256_cvttps_epi32(gatherMap(mapf, Bv)), Zv);
                const __m256i BBv = _mm256_add_epi32(_mm256_cvttps_epi32(gatherMap(mapf, _mm256_add_epi32(Bv, _mm256_set1_epi32(1)))), Zv);

                const __m256 gAA = gatherMap(mapf, AAv);
                const __m256 gBA = gatherMap(mapf, BAv);
                const __m256 gAB = gatherMap(mapf, ABv);
                const __m256 gBB = gatherMap(mapf, BBv);
                const __m256 gAA1 = gatherMap(mapf, _mm256_add_epi32(AAv, _mm256_set1_epi32(1)));
                const __m256 gBA1 = gatherMap(mapf, _mm256_add_epi32(BAv, _mm256_set1_epi32(1)));
                const __m256 gAB1 = gatherMap(mapf, _mm256_add_epi32(ABv, _mm256_set1_epi32(1)));
                const __m256 gBB1 = gatherMap(mapf, _mm256_add_epi32(BBv, _mm256_set1_epi32(1)));

                const __m256 yb1 = _mm256_sub_ps(y, _mm256_set1_ps(1.0f));
                const __m256 vv0 = lerp(ub, grad(gAA, xb, y, zb), grad(gBA, xb1, y, zb));
                const __m256 vv1 = lerp(ub, grad(gAB, xb, yb1, zb), grad(gBB, xb1, yb1, zb));
                const __m256 vv2 = lerp(ub, grad(gAA1, xb, y, zb1), grad(gBA1, xb1, y, zb1));
                const __m256 vv3 = lerp(ub, grad(gAB1, xb, yb1, zb1), grad(gBB1, xb1, yb1, zb1));
                const __m256 val = lerp(wb, lerp(v, vv0, vv1), lerp(v, vv2, vv3));
                accumulate(val, scale, col + yy);
            }
            if (yy < p.ySize) {
                const int n = p.ySize - yy;
                // Invalid lanes compute with out-of-run indices; their
                // gathers stay in range (Y is masked to 0..255) and the
                // results are discarded by accumulateTail.
                const __m256 yIdx = _mm256_cvtepi32_ps(
                    _mm256_add_epi32(_mm256_set1_epi32(yy), _mm256_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7)));
                __m256i Y;
                __m256 v;
                const __m256 y = calcValues(
                    _mm256_add_ps(_mm256_mul_ps(_mm256_add_ps(_mm256_set1_ps(p.py), yIdx),
                                                _mm256_set1_ps(p.sy)),
                                  _mm256_set1_ps(p.oy)),
                    Y, v);
                const __m256i Av = _mm256_add_epi32(Abase, Y);
                const __m256i Bv = _mm256_add_epi32(Bbase, Y);
                const __m256i AAv = _mm256_add_epi32(_mm256_cvttps_epi32(gatherMap(mapf, Av)), Zv);
                const __m256i ABv = _mm256_add_epi32(_mm256_cvttps_epi32(gatherMap(mapf, _mm256_add_epi32(Av, _mm256_set1_epi32(1)))), Zv);
                const __m256i BAv = _mm256_add_epi32(_mm256_cvttps_epi32(gatherMap(mapf, Bv)), Zv);
                const __m256i BBv = _mm256_add_epi32(_mm256_cvttps_epi32(gatherMap(mapf, _mm256_add_epi32(Bv, _mm256_set1_epi32(1)))), Zv);
                const __m256 gAA = gatherMap(mapf, AAv);
                const __m256 gBA = gatherMap(mapf, BAv);
                const __m256 gAB = gatherMap(mapf, ABv);
                const __m256 gBB = gatherMap(mapf, BBv);
                const __m256 gAA1 = gatherMap(mapf, _mm256_add_epi32(AAv, _mm256_set1_epi32(1)));
                const __m256 gBA1 = gatherMap(mapf, _mm256_add_epi32(BAv, _mm256_set1_epi32(1)));
                const __m256 gAB1 = gatherMap(mapf, _mm256_add_epi32(ABv, _mm256_set1_epi32(1)));
                const __m256 gBB1 = gatherMap(mapf, _mm256_add_epi32(BBv, _mm256_set1_epi32(1)));
                const __m256 yb1 = _mm256_sub_ps(y, _mm256_set1_ps(1.0f));
                const __m256 vv0 = lerp(ub, grad(gAA, xb, y, zb), grad(gBA, xb1, y, zb));
                const __m256 vv1 = lerp(ub, grad(gAB, xb, yb1, zb), grad(gBB, xb1, yb1, zb));
                const __m256 vv2 = lerp(ub, grad(gAA1, xb, y, zb1), grad(gBA1, xb1, y, zb1));
                const __m256 vv3 = lerp(ub, grad(gAB1, xb, yb1, zb1), grad(gBB1, xb1, yb1, zb1));
                const __m256 val = lerp(wb, lerp(v, vv0, vv1), lerp(v, vv2, vv3));
                accumulateTail(val, scale, col + yy, n);
            }
        }
    }
    return true;
}

} // namespace cursestone::performance::noise
#endif // x86
