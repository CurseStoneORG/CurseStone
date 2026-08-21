// PerlinNoise — classic 3D Perlin noise (Ken Perlin's improved
// noise). Used as a fallback generator and as the secondary
// octave layer for biome edge blending in Normal.
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mcp_perlin {
    int64_t seed;
    int32_t perm[512];
} mcp_perlin_t;

mcp_perlin_t* mcp_perlin_new(int64_t seed);
void          mcp_perlin_destroy(mcp_perlin_t* p);

double mcp_perlin_noise2d(const mcp_perlin_t* p, double x, double y);
double mcp_perlin_noise3d(const mcp_perlin_t* p, double x, double y, double z);
double mcp_perlin_fbm2d(const mcp_perlin_t* p, double x, double y,
                        int octaves, double persistence, double lacunarity);

#ifdef __cplusplus
}
#endif
