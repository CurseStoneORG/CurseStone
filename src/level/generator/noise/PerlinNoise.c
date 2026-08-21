#include "PerlinNoise.h"
#include <stdlib.h>
#include <math.h>

static double fade(double t) { return t * t * t * (t * (t * 6 - 15) + 10); }
static double lerp(double a, double b, double t) { return a + t * (b - a); }
static double grad(int hash, double x, double y, double z) {
    int h = hash & 15;
    double u = h < 8 ? x : y;
    double v = h < 4 ? y : (h == 12 || h == 14 ? x : z);
    return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
}

static uint32_t hash_seed(int64_t seed) {
    uint64_t x = (uint64_t)seed;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    x = (x ^ (x >> 31));
    return (uint32_t)(x & 0xffffffff);
}

mcp_perlin_t* mcp_perlin_new(int64_t seed) {
    mcp_perlin_t* p = (mcp_perlin_t*)malloc(sizeof(*p));
    if (!p) return NULL;
    p->seed = seed;
    int perm[256];
    for (int i = 0; i < 256; ++i) perm[i] = i;
    uint32_t state = hash_seed(seed);
    for (int i = 255; i > 0; --i) {
        state = (state * 1664525u) + 1013904223u;
        int j = (int)(state % (uint32_t)(i + 1));
        int t = perm[i]; perm[i] = perm[j]; perm[j] = t;
    }
    for (int i = 0; i < 256; ++i) {
        p->perm[i] = perm[i];
        p->perm[i + 256] = perm[i];
    }
    return p;
}

void mcp_perlin_destroy(mcp_perlin_t* p) {
    if (p) free(p);
}

double mcp_perlin_noise3d(const mcp_perlin_t* p, double x, double y, double z) {
    int X = (int)floor(x) & 255;
    int Y = (int)floor(y) & 255;
    int Z = (int)floor(z) & 255;
    x -= floor(x); y -= floor(y); z -= floor(z);
    double u = fade(x), v = fade(y), w = fade(z);
    int A  = p->perm[X] + Y, AA = p->perm[A] + Z, AB = p->perm[A + 1] + Z;
    int B  = p->perm[X + 1] + Y, BA = p->perm[B] + Z, BB = p->perm[B + 1] + Z;
    return lerp(
        lerp(lerp(grad(p->perm[AA], x, y, z),      grad(p->perm[BA], x-1, y, z),      u),
             lerp(grad(p->perm[AB], x, y-1, z),    grad(p->perm[BB], x-1, y-1, z),    u), v),
        lerp(lerp(grad(p->perm[AA+1], x, y, z-1),  grad(p->perm[BA+1], x-1, y, z-1),  u),
             lerp(grad(p->perm[AB+1], x, y-1, z-1),grad(p->perm[BB+1], x-1, y-1, z-1),u), v), w);
}

double mcp_perlin_noise2d(const mcp_perlin_t* p, double x, double y) {
    return mcp_perlin_noise3d(p, x, y, 0.0);
}

double mcp_perlin_fbm2d(const mcp_perlin_t* p, double x, double y,
                        int octaves, double persistence, double lacunarity) {
    double total = 0.0, freq = 1.0, amp = 1.0, max = 0.0;
    for (int i = 0; i < octaves; ++i) {
        total += mcp_perlin_noise2d(p, x * freq, y * freq) * amp;
        max += amp; amp *= persistence; freq *= lacunarity;
    }
    return max > 0.0 ? total / max : 0.0;
}
