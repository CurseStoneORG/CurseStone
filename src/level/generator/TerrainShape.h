// SPDX-License-Identifier: LGPL-3.0-or-later
// TerrainShape — pure, world-coordinate terrain functions.
//
// The whole point of this module is that terrain SHAPE and biome are a pure
// function of (seed, worldX, worldZ). Normal::generate() builds the height
// grid of a chunk through these functions, and the feature populators
// (trees) can evaluate the *neighbouring* chunks' columns without ever
// loading those chunks. That is what lets trees span chunk borders: every
// chunk re-renders the overlapping part of the trees of the 3x3 chunks
// around it, and because the shape is deterministic the pieces line up
// exactly.
#pragma once
#include "level/generator/noise/SimplexNoise.h"
#include <cstdint>
#include <cmath>

namespace cursestone::level::generator {

// World sea level. Rivers and the ocean surface both sit here, so a river
// always merges into the sea at exactly the same height.
inline constexpr int kSeaLevel = 64;

// Owns the noise layers: terrain shape, climate and rivers.
struct TerrainNoise {
    mcp_simplex_t* height = nullptr;   // continents + hills + ridges + detail
    mcp_simplex_t* temp   = nullptr;   // climate: temperature
    mcp_simplex_t* humid  = nullptr;   // climate: humidity
    mcp_simplex_t* river  = nullptr;   // river network (value crosses 0)

    explicit TerrainNoise(int64_t seed);
    ~TerrainNoise();
    TerrainNoise(const TerrainNoise&) = delete;
    TerrainNoise& operator=(const TerrainNoise&) = delete;
};

// Raw (pre-smoothing) column height for a world column.
double rawColumnHeight(const TerrainNoise& n, double wx, double wz);

// Apply the mild (1,2,1)^2/16 smoothing kernel to a 3x3 block of raw
// heights (raw[a][b] = rawColumnHeight(wx + a - 1, wz + b - 1)) and round.
int heightFromRaw(const double raw[3][3]);

// Final terrain height: the Y the terrain fill uses as its top+1 (the
// ground block sits at height-1, water fills height..kSeaLevel-1).
int columnHeight(const TerrainNoise& n, double wx, double wz);

// River proximity mask in [0,1]: 1 at the channel centre, 0 outside it.
// Rivers follow the zero contour of the river noise, so they form a
// connected network that cuts through hills and mountains.
double riverMask(const TerrainNoise& n, double wx, double wz);

// Biome id for a world column, given its height. Pure function of the
// world coordinates, matching what Normal::generate stores.
uint8_t columnBiome(const TerrainNoise& n, double wx, double wz, int h);

} // namespace cursestone::level::generator
