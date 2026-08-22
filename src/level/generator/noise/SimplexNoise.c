// SimplexNoise.c — pure-C implementation. See header for docs.
#include "SimplexNoise.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

// Gradients 3D (12 edges of (1,1,1)-cube, simplified triplets).
// Same set Gustavson uses in his reference implementation.
static const double grad3[12][3] = {
    {1,1,0},{-1,1,0},{1,-1,0},{-1,-1,0},
    {1,0,1},{-1,0,1},{1,0,-1},{-1,0,-1},
    {0,1,1},{0,-1,1},{0,1,-1},{0,-1,-1}
};

// Skewing factors for 3D: F3 = 1/3, G3 = 1/6.
#define F3 0.3333333333333333
#define G3 0.1666666666666667

static uint32_t hash_seed_u32(int64_t seed) {
    uint64_t x = (uint64_t)seed;
    x = (x ^ (x >> 33)) * 0xff51afd7ed558ccdULL;
    x = (x ^ (x >> 33)) * 0xc4ceb9fe1a85ec53ULL;
    x = (x ^ (x >> 33));
    return (uint32_t)(x & 0xffffffff);
}

static int fast_floor(double x) {
    int xi = (int)x;
    return x < xi ? xi - 1 : xi;
}

mcp_simplex_t* mcp_simplex_new(int64_t seed) {
    mcp_simplex_t* s = (mcp_simplex_t*)malloc(sizeof(*s));
    if (!s) return NULL;
    s->seed = seed;

    uint8_t p[256];
    for (int i = 0; i < 256; ++i) p[i] = (uint8_t)i;

    // Linear congruential PRNG seeded by world seed (deterministic).
    uint32_t state = hash_seed_u32(seed);
    for (int i = 255; i > 0; --i) {
        state = (state * 1664525u) + 1013904223u;
        uint32_t r = state;
        int j = (int)(r % (uint32_t)(i + 1));
        uint8_t t = p[i]; p[i] = p[j]; p[j] = t;
    }

    for (int i = 0; i < 256; ++i) {
        s->perm[i] = p[i];
        s->perm[i + 256] = p[i];
        s->permMod12[i] = (uint8_t)(p[i] % 12);
        s->permMod12[i + 256] = (uint8_t)(p[i] % 12);
    }
    return s;
}

void mcp_simplex_destroy(mcp_simplex_t* s) {
    if (s) free(s);
}

double mcp_simplex_noise2d(const mcp_simplex_t* s, double xin, double yin) {
    // 2D simplex (Gustavson's paper, simplified).
    const double F2 = 0.3660254037844386; // 0.5*(sqrt(3)-1)
    const double G2 = 0.2113248654051871; // (3 - sqrt(3))/6
    double s2 = (xin + yin) * F2;
    int i = fast_floor(xin + s2);
    int j = fast_floor(yin + s2);
    double t = (i + j) * G2;
    double X0 = i - t, Y0 = j - t;
    double x0 = xin - X0, y0 = yin - Y0;
    int i1, j1;
    if (x0 > y0) { i1 = 1; j1 = 0; } else { i1 = 0; j1 = 1; }
    double x1 = x0 - i1 + G2;
    double y1 = y0 - j1 + G2;
    double x2 = x0 - 1.0 + 2.0 * G2;
    double y2 = y0 - 1.0 + 2.0 * G2;
    int ii = i & 255, jj = j & 255;
    int gi0 = s->permMod12[ii + s->perm[jj]];
    int gi1 = s->permMod12[ii + i1 + s->perm[jj + j1]];
    int gi2 = s->permMod12[ii + 1 + s->perm[jj + 1]];

    double n0 = 0.0, n1 = 0.0, n2 = 0.0;
    double t0 = 0.5 - x0*x0 - y0*y0;
    if (t0 >= 0) { t0 *= t0; n0 = t0 * t0 * (grad3[gi0][0]*x0 + grad3[gi0][1]*y0); }
    double t1v = 0.5 - x1*x1 - y1*y1;
    if (t1v >= 0) { t1v *= t1v; n1 = t1v * t1v * (grad3[gi1][0]*x1 + grad3[gi1][1]*y1); }
    double t2 = 0.5 - x2*x2 - y2*y2;
    if (t2 >= 0) { t2 *= t2; n2 = t2 * t2 * (grad3[gi2][0]*x2 + grad3[gi2][1]*y2); }
    return 70.0 * (n0 + n1 + n2);
}

double mcp_simplex_noise3d(const mcp_simplex_t* s, double xin, double yin, double zin) {
    double n0, n1, n2, n3;
    double sk = (xin + yin + zin) * F3;
    int i = fast_floor(xin + sk);
    int j = fast_floor(yin + sk);
    int k = fast_floor(zin + sk);
    double t = (i + j + k) * G3;
    double X0 = i - t, Y0 = j - t, Z0 = k - t;
    double x0 = xin - X0, y0 = yin - Y0, z0 = zin - Z0;

    int i1, j1, k1, i2, j2, k2;
    if (x0 < y0) {
        if (y0 < z0)        { i1=0; j1=0; k1=1; i2=0; j2=1; k2=1; }
        else if (x0 < z0)   { i1=0; j1=1; k1=0; i2=0; j2=1; k2=1; }
        else                { i1=0; j1=1; k1=0; i2=1; j2=1; k2=0; }
    } else {
        if (x0 < z0)        { i1=1; j1=0; k1=0; i2=0; j2=0; k2=1; }
        else if (y0 < z0)  { i1=1; j1=0; k1=0; i2=0; j2=1; k2=1; }
        else                { i1=1; j1=0; k1=0; i2=1; j2=1; k2=0; }
    }

    double x1 = x0 - i1 + G3, y1 = y0 - j1 + G3, z1 = z0 - k1 + G3;
    double x2 = x0 - i2 + 2.0*G3, y2 = y0 - j2 + 2.0*G3, z2 = z0 - k2 + 2.0*G3;
    double x3 = x0 - 1.0 + 3.0*G3, y3 = y0 - 1.0 + 3.0*G3, z3 = z0 - 1.0 + 3.0*G3;
    int ii = i & 255, jj = j & 255, kk = k & 255;
    int gi0 = s->permMod12[ii + s->perm[jj + s->perm[kk]]];
    int gi1 = s->permMod12[ii + i1 + s->perm[jj + j1 + s->perm[kk + k1]]];
    int gi2 = s->permMod12[ii + i2 + s->perm[jj + j2 + s->perm[kk + k2]]];
    int gi3 = s->permMod12[ii + 1 + s->perm[jj + 1 + s->perm[kk + 1]]];

    double t0 = 0.6 - x0*x0 - y0*y0 - z0*z0;
    if (t0 < 0) n0 = 0.0; else { t0 *= t0; n0 = t0 * t0 * (grad3[gi0][0]*x0 + grad3[gi0][1]*y0 + grad3[gi0][2]*z0); }
    double t1 = 0.6 - x1*x1 - y1*y1 - z1*z1;
    if (t1 < 0) n1 = 0.0; else { t1 *= t1; n1 = t1 * t1 * (grad3[gi1][0]*x1 + grad3[gi1][1]*y1 + grad3[gi1][2]*z1); }
    double t2 = 0.6 - x2*x2 - y2*y2 - z2*z2;
    if (t2 < 0) n2 = 0.0; else { t2 *= t2; n2 = t2 * t2 * (grad3[gi2][0]*x2 + grad3[gi2][1]*y2 + grad3[gi2][2]*z2); }
    double t3 = 0.6 - x3*x3 - y3*y3 - z3*z3;
    if (t3 < 0) n3 = 0.0; else { t3 *= t3; n3 = t3 * t3 * (grad3[gi3][0]*x3 + grad3[gi3][1]*y3 + grad3[gi3][2]*z3); }
    return 32.0 * (n0 + n1 + n2 + n3);
}

double mcp_simplex_fbm2d(const mcp_simplex_t* s, double x, double y,
                         int octaves, double persistence, double lacunarity) {
    double total = 0.0, freq = 1.0, amp = 1.0, max = 0.0;
    for (int i = 0; i < octaves; ++i) {
        total += mcp_simplex_noise2d(s, x * freq, y * freq) * amp;
        max += amp; amp *= persistence; freq *= lacunarity;
    }
    return max > 0.0 ? total / max : 0.0;
}

double mcp_simplex_fbm3d(const mcp_simplex_t* s, double x, double y, double z,
                         int octaves, double persistence, double lacunarity) {
    double total = 0.0, freq = 1.0, amp = 1.0, max = 0.0;
    for (int i = 0; i < octaves; ++i) {
        total += mcp_simplex_noise3d(s, x * freq, y * freq, z * freq) * amp;
        max += amp; amp *= persistence; freq *= lacunarity;
    }
    return max > 0.0 ? total / max : 0.0;
}

uint32_t mcp_simplex_hash(int64_t seed, int32_t x, int32_t z) {
    uint64_t h = (uint64_t)seed;
    h ^= (uint64_t)(uint32_t)x * 0x9e3779b97f4a7c15ULL;
    h ^= (uint64_t)(uint32_t)z * 0xc2b2ae3d27d4eb4fULL;
    h ^= h >> 33; h *= 0xff51afd7ed558ccdULL;
    h ^= h >> 33; h *= 0xc4ceb9fe1a85ec53ULL;
    h ^= h >> 33;
    return (uint32_t)(h & 0xffffffff);
}
