// SPDX-License-Identifier: LGPL-3.0-or-later
#include "level/generator/TerrainShape.h"
#include "level/generator/biome/BiomeRegistry.h"
#include "level/generator/biome/Biome.h"

#include <algorithm>

namespace cursestone::level::generator {

namespace {

inline double smoothstep(double e0, double e1, double x) {
    double t = (x - e0) / (e1 - e0);
    if (t < 0.0) t = 0.0;
    else if (t > 1.0) t = 1.0;
    return t * t * (3.0 - 2.0 * t);
}

// Land mask: 0 over deep ocean, 1 on the continent.
inline double landMask(double cont) { return smoothstep(-0.28, 0.02, cont); }

} // namespace

TerrainNoise::TerrainNoise(int64_t seed) {
    height = mcp_simplex_new(seed);
    temp   = mcp_simplex_new(seed ^ 0xBA5EBA11);
    humid  = mcp_simplex_new(seed ^ 0x0FF1CE);
    river  = mcp_simplex_new(seed ^ 0x517E12);
}

TerrainNoise::~TerrainNoise() {
    mcp_simplex_destroy(height);
    mcp_simplex_destroy(temp);
    mcp_simplex_destroy(humid);
    mcp_simplex_destroy(river);
}

double rawColumnHeight(const TerrainNoise& n, double wx, double wz) {
    // Continent shape (oceans vs. land). Long wavelength.
    const double cont = mcp_simplex_fbm2d(n.height, wx * 0.0010, wz * 0.0010,
                                          3, 0.5, 2.0);
    // Rolled hills.
    const double hill = mcp_simplex_fbm2d(n.height, wx * 0.0055 + 77.3,
                                          wz * 0.0055 - 41.7, 4, 0.5, 2.0);
    // Small surface detail — this is what keeps the terrain from looking
    // like a smooth ramp.
    const double det = mcp_simplex_fbm2d(n.height, wx * 0.0140 + 900.0,
                                         wz * 0.0140 + 300.0, 3, 0.5, 2.0);
    // Ridged mountains: a value that folds at 0 into sharp crests.
    const double ridgeN = mcp_simplex_fbm2d(n.height, wx * 0.0032 + 1234.5,
                                            wz * 0.0032 - 987.6, 3, 0.5, 2.0);
    double ridge = 1.0 - std::fabs(ridgeN);
    ridge *= ridge;

    const double land = landMask(cont);

    double h = 70.0 + cont * 31.0;                       // 39 .. 101 base
    h += hill * (19.0 * land + 3.0 * (1.0 - land));      // hills
    h += ridge * 22.0 * land * smoothstep(0.30, 0.62, cont); // mountains (rare)
    h += det * (4.0 * land + 0.6);                       // rough detail

    // ---- Rivers ---------------------------------------------------------
    // Carve the channel down to just below sea level wherever the river
    // mask is strong, regardless of how high the surrounding terrain is —
    // this is what makes a river cut straight through a hill or mountain.
    const double rm = riverMask(n, wx, wz) * land;
    if (rm > 0.0) {
        // Deep enough that even a tall mountain gets a water-filled channel
        // after the smoothing blur averages the banks in.
        const double bed = (double)kSeaLevel - 5.0;
        h = h * (1.0 - rm) + bed * rm;
    }

    return std::clamp(h, 10.0, 122.0);
}

int heightFromRaw(const double raw[3][3]) {
    // (1,2,1) * (1,2,1) / 16 — the two separable blur passes of the
    // original generator, collapsed into the 3x3 kernel it is equivalent
    // to. Keeping this mild keeps the terrain rough but removes the
    // 1-block terracing noise would otherwise show.
    static const double k[3] = {1.0, 2.0, 1.0};
    double sum = 0.0;
    for (int a = 0; a < 3; ++a)
        for (int b = 0; b < 3; ++b)
            sum += k[a] * k[b] * raw[a][b];
    int h = (int)std::lround(sum / 16.0);
    return std::clamp(h, 8, 124);
}

int columnHeight(const TerrainNoise& n, double wx, double wz) {
    double raw[3][3];
    for (int a = 0; a < 3; ++a)
        for (int b = 0; b < 3; ++b)
            raw[a][b] = rawColumnHeight(n, wx + a - 1.0, wz + b - 1.0);
    return heightFromRaw(raw);
}

double riverMask(const TerrainNoise& n, double wx, double wz) {
    // Rivers are the zero contour of this low-frequency field. |value|
    // is small along the contour, so it becomes a smooth channel.
    const double r = mcp_simplex_fbm2d(n.river, wx * 0.0013, wz * 0.0013,
                                       2, 0.5, 2.0);
    const double d = std::fabs(r);
    constexpr double kCore = 0.055;   // fully carved channel floor
    constexpr double kEdge = 0.165;   // river bank
    if (d >= kEdge) return 0.0;
    if (d <= kCore) return 1.0;
    const double t = (d - kCore) / (kEdge - kCore);
    return 1.0 - t * t * (3.0 - 2.0 * t);
}

uint8_t columnBiome(const TerrainNoise& n, double wx, double wz, int h) {
    using namespace biome;

    const double t  = mcp_simplex_fbm2d(n.temp, wx * 0.0004, wz * 0.0004,
                                        2, 0.5, 2.0);
    const double hu = mcp_simplex_fbm2d(n.humid, wx * 0.0005 + 512.0,
                                        wz * 0.0005 - 256.0, 2, 0.5, 2.0);

    // Below sea level minus a shallow margin: open ocean.
    if (h < kSeaLevel - 2) return BIOME_OCEAN;

    // River channel: carved columns that sit below the water surface, so
    // the channel is always filled with water at sea level.
    if (h < kSeaLevel && riverMask(n, wx, wz) > 0.25)
        return (t < -0.25) ? BIOME_FROZEN_RIVER : BIOME_RIVER;

    uint8_t b = BiomeRegistry::select(t, hu, h, kSeaLevel);
    if (b == BIOME_OCEAN) b = BIOME_PLAINS;

    // Beach: a land column at/just above sea level touching open water.
    if (h <= kSeaLevel + 2) {
        constexpr double d = 1.0;
        if (columnHeight(n, wx - d, wz) < kSeaLevel - 2 ||
            columnHeight(n, wx + d, wz) < kSeaLevel - 2 ||
            columnHeight(n, wx, wz - d) < kSeaLevel - 2 ||
            columnHeight(n, wx, wz + d) < kSeaLevel - 2)
            return BIOME_BEACH;
    }
    return b;
}

} // namespace cursestone::level::generator
