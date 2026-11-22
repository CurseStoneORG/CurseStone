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
// NEON kernels for the vanilla Perlin octave regions (AArch64). Same
// contract as the AVX2 file: exact IEEE ops only (mul/sub/add/cmp), no
// fused multiply-accumulate intrinsics, scalar op order, 4 lanes.
// AArch64 has no vector gather, so the two-level table walk runs as
// per-lane scalar lookups batched through lookup4; the win here is the
// vectorized fade/gradient/lerp math.
#include "NoiseOptimizer.h"

#if defined(__aarch64__)
#include <arm_neon.h>

namespace cursestone::performance::noise {
namespace {

constexpr int kLanes = 4;

static inline float32x4_t calcValues(float32x4_t val, int32x4_t& index, float32x4_t& fade) {
    const int32x4_t trunc = vcvtzq_s32_f32(val);
    const uint32x4_t lt = vcltq_f32(val, vcvtq_f32_s32(trunc));
    const int32x4_t vf = vaddq_s32(trunc, vreinterpretq_s32_u32(lt));
    index = vandq_s32(vf, vdupq_n_s32(255));
    const float32x4_t frac = vsubq_f32(val, vcvtq_f32_s32(vf));
    const float32x4_t y2 = vmulq_f32(frac, frac);
    const float32x4_t y3 = vmulq_f32(y2, frac);
    const float32x4_t t6 = vmulq_f32(frac, vdupq_n_f32(6.0f));
    const float32x4_t t15 = vsubq_f32(t6, vdupq_n_f32(15.0f));
    const float32x4_t t10 = vaddq_f32(vmulq_f32(frac, t15), vdupq_n_f32(10.0f));
    fade = vmulq_f32(y3, t10);
    return frac;
}

static inline float32x4_t lerp(float32x4_t t, float32x4_t a, float32x4_t b) {
    return vaddq_f32(a, vmulq_f32(t, vsubq_f32(b, a)));
}

static inline float32x4_t grad(float32x4_t hashF, float32x4_t px, float32x4_t py, float32x4_t pz) {
    const int32x4_t h = vandq_s32(vcvtzq_s32_f32(hashF), vdupq_n_s32(15));
    // u = h < 8 ? px : py
    const float32x4_t u = vbslq_f32(vcltq_s32(h, vdupq_n_s32(8)), px, py);
    // v = h < 4 ? py : (h == 12 || h == 14 ? px : pz)
    const uint32x4_t m12or14 = vorrq_u32(vceqq_s32(h, vdupq_n_s32(12)),
                                         vceqq_s32(h, vdupq_n_s32(14)));
    const float32x4_t vInner = vbslq_f32(m12or14, px, pz);
    const float32x4_t v = vbslq_f32(vcltq_s32(h, vdupq_n_s32(4)), py, vInner);
    // ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v)
    const float32x4_t r1 = vbslq_f32(vceqzq_s32(vandq_s32(h, vdupq_n_s32(1))), u, vnegq_f32(u));
    const float32x4_t r2 = vbslq_f32(vceqzq_s32(vandq_s32(h, vdupq_n_s32(2))), v, vnegq_f32(v));
    return vaddq_f32(r1, r2);
}

// _grad2(hash, x, z): u = (1 - ((h & 8) >> 3)) * x  ->  h & 8 ? 0 : x.
static inline float32x4_t grad2(float32x4_t hashF, float32x4_t x, float32x4_t z) {
    const int32x4_t h = vandq_s32(vcvtzq_s32_f32(hashF), vdupq_n_s32(15));
    const float32x4_t u = vbslq_f32(vceqzq_s32(vandq_s32(h, vdupq_n_s32(8))), x, vdupq_n_f32(0));
    const uint32x4_t m12or14 = vorrq_u32(vceqq_s32(h, vdupq_n_s32(12)),
                                         vceqq_s32(h, vdupq_n_s32(14)));
    const float32x4_t vInner = vbslq_f32(m12or14, x, z);
    const float32x4_t v = vbslq_f32(vcltq_s32(h, vdupq_n_s32(4)), vdupq_n_f32(0), vInner);
    const float32x4_t r1 = vbslq_f32(vceqzq_s32(vandq_s32(h, vdupq_n_s32(1))), u, vnegq_f32(u));
    const float32x4_t r2 = vbslq_f32(vceqzq_s32(vandq_s32(h, vdupq_n_s32(2))), v, vnegq_f32(v));
    return vaddq_f32(r1, r2);
}

// Per-lane table read with in-range clamping (garbage lanes are
// discarded by the caller).
static inline float32x4_t lookup4(const float* mapf, int32x4_t idx) {
    int32_t i[kLanes];
    vst1q_s32(i, idx);
    float tmp[kLanes];
    for (int k = 0; k < kLanes; ++k) {
        int j = i[k];
        j = j < 0 ? 0 : (j > 511 ? 511 : j);
        tmp[k] = mapf[j];
    }
    return vld1q_f32(tmp);
}

static inline void accumulate(float32x4_t val, float scale, float* buf) {
    const float32x4_t cur = vld1q_f32(buf);
    vst1q_f32(buf, vaddq_f32(cur, vmulq_f32(val, vdupq_n_f32(scale))));
}

static inline void accumulateTail(float32x4_t val, float scale, float* buf, int n) {
    alignas(16) float tmp[kLanes];
    vst1q_f32(tmp, val);
    for (int i = 0; i < n; ++i)
        buf[i] += tmp[i] * scale;
}

} // namespace

bool readArea2D_neon(const Params& p) {
    const float* mapf = p.mapf;
    if (!mapf)
        return false;
    const int total = p.xSize * p.zSize;
    const float scale = 1.0f / p.pow;
    const float32x4_t oxf = vdupq_n_f32(p.px);
    const float32x4_t osz = vdupq_n_f32(p.pz);
    const float32x4_t sxv = vdupq_n_f32(p.sx);
    const float32x4_t szv = vdupq_n_f32(p.sz);
    const float32x4_t oxv = vdupq_n_f32(p.ox);
    const float32x4_t ozv = vdupq_n_f32(p.oz);

    int pp = 0;
    for (; pp + kLanes <= total; pp += kLanes) {
        int xs[kLanes], zs[kLanes];
        for (int i = 0; i < kLanes; ++i) {
            xs[i] = (pp + i) / p.zSize;
            zs[i] = (pp + i) % p.zSize;
        }
        const float32x4_t xf = vcvtq_f32_s32(vld1q_s32(xs));
        const float32x4_t zf = vcvtq_f32_s32(vld1q_s32(zs));

        int32x4_t X, Z;
        float32x4_t u, w;
        const float32x4_t x = calcValues(vaddq_f32(vmulq_f32(vaddq_f32(oxf, xf), sxv), oxv), X, u);
        const float32x4_t z = calcValues(vaddq_f32(vmulq_f32(vaddq_f32(osz, zf), szv), ozv), Z, w);

        int32_t xi[kLanes], zi[kLanes];
        vst1q_s32(xi, X);
        vst1q_s32(zi, Z);

        // A = m[X], B = m[X + 1], AA = m[A] + Z, BA = m[B] + Z.
        int32_t ai[kLanes], bi[kLanes];
        float aaF[kLanes], baF[kLanes], aa1F[kLanes], ba1F[kLanes];
        for (int k = 0; k < kLanes; ++k) {
            ai[k] = p.map[xi[k]];
            bi[k] = p.map[xi[k] + 1];
        }
        for (int k = 0; k < kLanes; ++k) {
            aaF[k] = (float)p.map[ai[k]];
            baF[k] = (float)p.map[bi[k]];
        }
        // Corner "+1" hashes in the 2D path apply to the FULL index
        // m[A] + Z + 1 (the scalar loop uses mNoiseMap[AA + 1]), not to
        // m[A] alone.
        for (int k = 0; k < kLanes; ++k) {
            aa1F[k] = (float)p.map[p.map[ai[k]] + zi[k] + 1];
            ba1F[k] = (float)p.map[p.map[bi[k]] + zi[k] + 1];
        }
        const int32x4_t AAv = vaddq_s32(vcvtzq_s32_f32(vld1q_f32(aaF)), Z);
        const int32x4_t BAv = vaddq_s32(vcvtzq_s32_f32(vld1q_f32(baF)), Z);
        const int32x4_t AA1v = vaddq_s32(vcvtzq_s32_f32(vld1q_f32(aa1F)), Z);
        const int32x4_t BA1v = vaddq_s32(vcvtzq_s32_f32(vld1q_f32(ba1F)), Z);

        const float32x4_t gAA = lookup4(mapf, AAv);
        const float32x4_t gBA = lookup4(mapf, BAv);
        const float32x4_t gAA1 = lookup4(mapf, AA1v);
        const float32x4_t gBA1 = lookup4(mapf, BA1v);

        const float32x4_t x1 = vsubq_f32(x, vdupq_n_f32(1.0f));
        const float32x4_t z1 = vsubq_f32(z, vdupq_n_f32(1.0f));
        const float32x4_t zero = vdupq_n_f32(0.0f);
        const float32x4_t vv0 = lerp(u, grad2(gAA, x, z), grad(gBA, x1, zero, z));
        const float32x4_t vv2 = lerp(u, grad(gAA1, x, zero, z1), grad(gBA1, x1, zero, z1));
        const float32x4_t val = lerp(w, vv0, vv2);
        accumulate(val, scale, p.buffer + pp);
    }
    if (pp < total) {
        const int n = total - pp;
        int xs[kLanes], zs[kLanes];
        for (int i = 0; i < kLanes; ++i) {
            const int idx = pp + (i < n ? i : 0);
            xs[i] = idx / p.zSize;
            zs[i] = idx % p.zSize;
        }
        const float32x4_t xf = vcvtq_f32_s32(vld1q_s32(xs));
        const float32x4_t zf = vcvtq_f32_s32(vld1q_s32(zs));
        int32x4_t X, Z;
        float32x4_t u, w;
        const float32x4_t x = calcValues(vaddq_f32(vmulq_f32(vaddq_f32(vdupq_n_f32(p.px), xf), sxv), oxv), X, u);
        const float32x4_t z = calcValues(vaddq_f32(vmulq_f32(vaddq_f32(vdupq_n_f32(p.pz), zf), szv), ozv), Z, w);
        int32_t xi[kLanes], zi[kLanes];
        vst1q_s32(xi, X);
        vst1q_s32(zi, Z);
        int32_t ai[kLanes], bi[kLanes];
        float aaF[kLanes], baF[kLanes], aa1F[kLanes], ba1F[kLanes];
        for (int k = 0; k < kLanes; ++k) {
            ai[k] = p.map[xi[k]];
            bi[k] = p.map[xi[k] + 1];
        }
        for (int k = 0; k < kLanes; ++k) {
            aaF[k] = (float)p.map[ai[k]];
            baF[k] = (float)p.map[bi[k]];
        }
        // Corner "+1" hashes in the 2D path apply to the FULL index
        // m[A] + Z + 1 (the scalar loop uses mNoiseMap[AA + 1]), not to
        // m[A] alone.
        for (int k = 0; k < kLanes; ++k) {
            aa1F[k] = (float)p.map[p.map[ai[k]] + zi[k] + 1];
            ba1F[k] = (float)p.map[p.map[bi[k]] + zi[k] + 1];
        }
        const int32x4_t AAv = vaddq_s32(vcvtzq_s32_f32(vld1q_f32(aaF)), Z);
        const int32x4_t BAv = vaddq_s32(vcvtzq_s32_f32(vld1q_f32(baF)), Z);
        const int32x4_t AA1v = vaddq_s32(vcvtzq_s32_f32(vld1q_f32(aa1F)), Z);
        const int32x4_t BA1v = vaddq_s32(vcvtzq_s32_f32(vld1q_f32(ba1F)), Z);
        const float32x4_t gAA = lookup4(mapf, AAv);
        const float32x4_t gBA = lookup4(mapf, BAv);
        const float32x4_t gAA1 = lookup4(mapf, AA1v);
        const float32x4_t gBA1 = lookup4(mapf, BA1v);
        const float32x4_t x1 = vsubq_f32(x, vdupq_n_f32(1.0f));
        const float32x4_t z1 = vsubq_f32(z, vdupq_n_f32(1.0f));
        const float32x4_t zero = vdupq_n_f32(0.0f);
        const float32x4_t vv0 = lerp(u, grad2(gAA, x, z), grad(gBA, x1, zero, z));
        const float32x4_t vv2 = lerp(u, grad(gAA1, x, zero, z1), grad(gBA1, x1, zero, z1));
        const float32x4_t val = lerp(w, vv0, vv2);
        accumulateTail(val, scale, p.buffer + pp, n);
    }
    return true;
}

bool readArea3D_neon(const Params& p) {
    const float* mapf = p.mapf;
    if (!mapf || p.ySize <= 1)
        return false;
    const float scale = 1.0f / p.pow;

    for (int xx = 0; xx < p.xSize; xx++) {
        int X, Z;
        float u, w;
        float x = (p.px + xx) * p.sx + p.ox;
        {
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

            const int32x4_t Abase = vdupq_n_s32(p.map[X]);
            const int32x4_t Bbase = vdupq_n_s32(p.map[X + 1]);
            const int32x4_t Zv = vdupq_n_s32(Z);
            const float32x4_t xb = vdupq_n_f32(x);
            const float32x4_t xb1 = vdupq_n_f32(x - 1.0f);
            const float32x4_t zb = vdupq_n_f32(zc);
            const float32x4_t zb1 = vdupq_n_f32(zc - 1.0f);
            const float32x4_t ub = vdupq_n_f32(u);
            const float32x4_t wb = vdupq_n_f32(w);

            float* col = p.buffer + (xx * p.zSize + zz) * p.ySize;
            int yy = 0;
            for (; yy + kLanes <= p.ySize; yy += kLanes) {
                const int ys[kLanes] = {yy, yy + 1, yy + 2, yy + 3};
                const float32x4_t yIdx = vcvtq_f32_s32(vld1q_s32(ys));
                int32x4_t Y;
                float32x4_t v;
                const float32x4_t y = calcValues(
                    vaddq_f32(vmulq_f32(vaddq_f32(vdupq_n_f32(p.py), yIdx), vdupq_n_f32(p.sy)),
                              vdupq_n_f32(p.oy)),
                    Y, v);

                const int32x4_t Av = vaddq_s32(Abase, Y);
                const int32x4_t Bv = vaddq_s32(Bbase, Y);
                int32_t ai[kLanes], bi[kLanes];
                vst1q_s32(ai, Av);
                vst1q_s32(bi, Bv);
                float aF[kLanes], a1F[kLanes], bF[kLanes], b1F[kLanes];
                for (int k = 0; k < kLanes; ++k) {
                    aF[k] = (float)p.map[ai[k]];
                    a1F[k] = (float)p.map[ai[k] + 1];
                    bF[k] = (float)p.map[bi[k]];
                    b1F[k] = (float)p.map[bi[k] + 1];
                }
                const int32x4_t AAv = vaddq_s32(vcvtzq_s32_f32(vld1q_f32(aF)), Zv);
                const int32x4_t ABv = vaddq_s32(vcvtzq_s32_f32(vld1q_f32(a1F)), Zv);
                const int32x4_t BAv = vaddq_s32(vcvtzq_s32_f32(vld1q_f32(bF)), Zv);
                const int32x4_t BBv = vaddq_s32(vcvtzq_s32_f32(vld1q_f32(b1F)), Zv);

                const float32x4_t gAA = lookup4(mapf, AAv);
                const float32x4_t gBA = lookup4(mapf, BAv);
                const float32x4_t gAB = lookup4(mapf, ABv);
                const float32x4_t gBB = lookup4(mapf, BBv);
                const float32x4_t gAA1 = lookup4(mapf, vaddq_s32(AAv, vdupq_n_s32(1)));
                const float32x4_t gBA1 = lookup4(mapf, vaddq_s32(BAv, vdupq_n_s32(1)));
                const float32x4_t gAB1 = lookup4(mapf, vaddq_s32(ABv, vdupq_n_s32(1)));
                const float32x4_t gBB1 = lookup4(mapf, vaddq_s32(BBv, vdupq_n_s32(1)));

                const float32x4_t yb1 = vsubq_f32(y, vdupq_n_f32(1.0f));
                const float32x4_t vv0 = lerp(ub, grad(gAA, xb, y, zb), grad(gBA, xb1, y, zb));
                const float32x4_t vv1 = lerp(ub, grad(gAB, xb, yb1, zb), grad(gBB, xb1, yb1, zb));
                const float32x4_t vv2 = lerp(ub, grad(gAA1, xb, y, zb1), grad(gBA1, xb1, y, zb1));
                const float32x4_t vv3 = lerp(ub, grad(gAB1, xb, yb1, zb1), grad(gBB1, xb1, yb1, zb1));
                const float32x4_t val = lerp(wb, lerp(v, vv0, vv1), lerp(v, vv2, vv3));
                accumulate(val, scale, col + yy);
            }
            if (yy < p.ySize) {
                const int n = p.ySize - yy;
                const int ys[kLanes] = {yy, yy + 1, yy + 2, yy + 3};
                const float32x4_t yIdx = vcvtq_f32_s32(vld1q_s32(ys));
                int32x4_t Y;
                float32x4_t v;
                const float32x4_t y = calcValues(
                    vaddq_f32(vmulq_f32(vaddq_f32(vdupq_n_f32(p.py), yIdx), vdupq_n_f32(p.sy)),
                              vdupq_n_f32(p.oy)),
                    Y, v);
                const int32x4_t Av = vaddq_s32(Abase, Y);
                const int32x4_t Bv = vaddq_s32(Bbase, Y);
                int32_t ai[kLanes], bi[kLanes];
                vst1q_s32(ai, Av);
                vst1q_s32(bi, Bv);
                float aF[kLanes], a1F[kLanes], bF[kLanes], b1F[kLanes];
                for (int k = 0; k < kLanes; ++k) {
                    aF[k] = (float)p.map[ai[k]];
                    a1F[k] = (float)p.map[ai[k] + 1];
                    bF[k] = (float)p.map[bi[k]];
                    b1F[k] = (float)p.map[bi[k] + 1];
                }
                const int32x4_t AAv = vaddq_s32(vcvtzq_s32_f32(vld1q_f32(aF)), Zv);
                const int32x4_t ABv = vaddq_s32(vcvtzq_s32_f32(vld1q_f32(a1F)), Zv);
                const int32x4_t BAv = vaddq_s32(vcvtzq_s32_f32(vld1q_f32(bF)), Zv);
                const int32x4_t BBv = vaddq_s32(vcvtzq_s32_f32(vld1q_f32(b1F)), Zv);
                const float32x4_t gAA = lookup4(mapf, AAv);
                const float32x4_t gBA = lookup4(mapf, BAv);
                const float32x4_t gAB = lookup4(mapf, ABv);
                const float32x4_t gBB = lookup4(mapf, BBv);
                const float32x4_t gAA1 = lookup4(mapf, vaddq_s32(AAv, vdupq_n_s32(1)));
                const float32x4_t gBA1 = lookup4(mapf, vaddq_s32(BAv, vdupq_n_s32(1)));
                const float32x4_t gAB1 = lookup4(mapf, vaddq_s32(ABv, vdupq_n_s32(1)));
                const float32x4_t gBB1 = lookup4(mapf, vaddq_s32(BBv, vdupq_n_s32(1)));
                const float32x4_t yb1 = vsubq_f32(y, vdupq_n_f32(1.0f));
                const float32x4_t vv0 = lerp(ub, grad(gAA, xb, y, zb), grad(gBA, xb1, y, zb));
                const float32x4_t vv1 = lerp(ub, grad(gAB, xb, yb1, zb), grad(gBB, xb1, yb1, zb));
                const float32x4_t vv2 = lerp(ub, grad(gAA1, xb, y, zb1), grad(gBA1, xb1, y, zb1));
                const float32x4_t vv3 = lerp(ub, grad(gAB1, xb, yb1, zb1), grad(gBB1, xb1, yb1, zb1));
                const float32x4_t val = lerp(wb, lerp(v, vv0, vv1), lerp(v, vv2, vv3));
                accumulateTail(val, scale, col + yy, n);
            }
        }
    }
    return true;
}

} // namespace cursestone::performance::noise
#endif // __aarch64__
