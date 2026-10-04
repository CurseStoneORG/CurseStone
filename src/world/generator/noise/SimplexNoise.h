// SimplexNoise — pure-C 2D/3D simplex noise.
// Based on Stefan Gustavson's reference implementation. Used by
// the Normal generator for terrain height, biome temperature and
// cave carving. The permutation table is seeded at construction
// time, so each world seed yields deterministic terrain.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mcp_simplex {
    uint8_t perm[512];     // doubled permutation table for indexing
    uint8_t permMod12[512]; // mod-12 table (corners)
    int64_t seed;
} mcp_simplex_t;

mcp_simplex_t* mcp_simplex_new(int64_t seed);
void           mcp_simplex_destroy(mcp_simplex_t* s);

// Single-octave noise in [−1, 1]. Output is *not* normalised.
double mcp_simplex_noise2d(const mcp_simplex_t* s, double x, double y);
double mcp_simplex_noise3d(const mcp_simplex_t* s, double x, double y, double z);

// Fractal Brownian Motion (multiple octaves). Persistence is the
// amplitude multiplier per octave; lacunarity is the frequency
// multiplier. Returns roughly [−1, 1].
double mcp_simplex_fbm2d(const mcp_simplex_t* s, double x, double y,
                         int octaves, double persistence, double lacunarity);
double mcp_simplex_fbm3d(const mcp_simplex_t* s, double x, double y, double z,
                         int octaves, double persistence, double lacunarity);

// Seeded hash used by the permutation table generator and by other
// generators (ore placement, tree position selection) that need a
// per-chunk deterministic PRNG.
uint32_t mcp_simplex_hash(int64_t seed, int32_t x, int32_t z);

#ifdef __cplusplus
}
#endif
