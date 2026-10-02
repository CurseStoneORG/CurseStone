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
    height  = mcp_simplex_new(seed);
    temp    = mcp_simplex_new(seed ^ 0xBA5EBA11);
    humid   = mcp_simplex_new(seed ^ 0x0FF1CE);
    river   = mcp_simplex_new(seed ^ 0x517E12);
    surface = mcp_simplex_new(seed ^ 0x9E3779B9);
}

TerrainNoise::~TerrainNoise() {
    mcp_simplex_destroy(height);
    mcp_simplex_destroy(temp);
    mcp_simplex_destroy(humid);
    mcp_simplex_destroy(river);
    mcp_simplex_destroy(surface);
}

double rawColumnHeight(const TerrainNoise& n, double wx, double wz) {
    // Continent shape (oceans vs. land). Long wavelength.
    const double cont = mcp_simplex_fbm2d(n.height, wx * 0.0010, wz * 0.0010,
                                          3, 0.5, 2.0);
    // Broad rolling swells — the large-scale swell that makes hills read as
    // hills instead of as one uniform bump field.
    const double broad = mcp_simplex_fbm2d(n.height, wx * 0.0022 + 313.0,
                                           wz * 0.0022 + 727.0, 3, 0.5, 2.0);
    // Rolled hills.
    const double hill = mcp_simplex_fbm2d(n.height, wx * 0.0055 + 77.3,
                                          wz * 0.0055 - 41.7, 4, 0.5, 2.0);
    // Small surface detail — this is what keeps the terrain from looking
    // like a smooth ramp.
    const double det = mcp_simplex_fbm2d(n.height, wx * 0.0140 + 900.0,
                                         wz * 0.0140 + 300.0, 3, 0.5, 2.0);
    // Fine crags. The long-wavelength layers barely move between two
    // neighbouring columns, so without a short-wavelength layer the ground
    // reads as glassy smooth no matter how big the hills are. This is what
    // actually gives the surface its local roughness.
    const double crag = mcp_simplex_fbm2d(n.height, wx * 0.0320 - 555.0,
                                          wz * 0.0320 + 111.0, 2, 0.5, 2.0);
    // Ridged mountains: a value that folds at 0 into sharp crests.
    const double ridgeN = mcp_simplex_fbm2d(n.height, wx * 0.0032 + 1234.5,
                                            wz * 0.0032 - 987.6, 3, 0.5, 2.0);
    double ridge = 1.0 - std::fabs(ridgeN);
    ridge *= ridge;
    // Sharp rocky outcrops. Every other layer here is low-frequency, so it
    // can only ever move the ground by a block or two between neighbouring
    // columns — which is why the hills read as smooth ramps. This is a
    // higher-frequency folded noise, masked to high ground only, and it is
    // what finally produces real 3-block steps and bare rock faces on the
    // hills without touching the lowland. Keeping it out of the lowlands is
    // also what stops it from making the river banks harsher.
    const double outN = mcp_simplex_fbm2d(n.height, wx * 0.0210 + 88.0,
                                          wz * 0.0210 - 55.0, 2, 0.5, 2.0);
    double outcrop = 1.0 - std::fabs(outN);
    outcrop = outcrop * outcrop * outcrop;

    const double land = landMask(cont);

    double h = 70.0 + cont * 31.0;                       // 39 .. 101 base
    h += broad * (13.0 * land + 1.5 * (1.0 - land));     // large swells
    h += hill * (16.0 * land + 3.0 * (1.0 - land));      // hills
    h += ridge * 24.0 * land * smoothstep(0.30, 0.62, cont); // mountains (rare)
    // Detail is boosted where the ground is already high: highland gets
    // craggier, lowland stays smooth, which is what vanilla does.
    const double highland = smoothstep(70.0, 92.0, h);
    h += det * (3.6 * land + 0.6) * (1.0 + 0.8 * highland);
    h += crag * (5.5 * land + 0.3);
    h += (outcrop - 0.30) * 9.5 * smoothstep(84.0, 100.0, h) *
         smoothstep(0.18, 0.55, cont);

    // ---- Rivers ---------------------------------------------------------
    // Three stages, on purpose. A WIDE valley lowers the land into a gentle
    // bowl, a still wider SKIRT eases the approach, and only then does the
    // NARROW river mask cut a thin channel into the bottom of that bowl.
    // Blending straight to a fixed deep bed (what this used to do) is what
    // produced slot canyons with near-vertical walls through the mountains
    // instead of the soft, wide valleys vanilla has.
    const double rv = riverValley(n, wx, wz) * land;
    if (rv > 0.0) {
        // Depth scales with how high the ground stands above sea level, so
        // a hill becomes a valley the river runs along — but it is CAPPED,
        // so even a tall mountain gets a broad valley rather than a gorge.
        const double above = std::max(0.0, h - (double)kSeaLevel);
        const double depth = std::min(5.0 + above * 0.40, 13.0);
        h -= rv * rv * (3.0 - 2.0 * rv) * depth;
    }

    // The wide, shallow skirt: same idea, spread much further out so the
    // ground eases down to the valley instead of dropping off it.
    const double rs = riverShoulder(n, wx, wz) * land;
    if (rs > 0.0) {
        // Flat ground barely dips. A shoulder that shaved several blocks off
        // EVERY nearby column, over an area this wide, pushed whole regions
        // below the water line and inflated the ocean from ~38% to ~41%.
        // The skirt only really matters where there is height to ease away
        // from.
        const double above = std::max(0.0, h - (double)kSeaLevel);
        const double depth = std::min(0.8 + above * 0.38, 9.0);
        h -= rs * rs * (3.0 - 2.0 * rs) * depth;
    }

    const double rm = riverMask(n, wx, wz) * land;
    if (rm > 0.0) {
        // Bed a few blocks under the water surface so the channel always
        // holds water at exactly kSeaLevel. min() guarantees the cut only
        // ever lowers the ground — blending to a fixed bed used to RAISE
        // low ground back up and leave the river dry.
        const double bed = (double)kSeaLevel - 4.0;
        h += (std::min(h, bed) - h) * rm;
    }

    return std::clamp(h, 10.0, 122.0);
}

int heightFromRaw(const double raw[3][3]) {
    // (1,2,1) * (1,2,1) / 16 — the two separable blur passes of the
    // original generator, collapsed into the 3x3 kernel it is equivalent
    // to. Keeping this mild keeps the terrain rough but removes the
    // 1-block terracing noise would otherwise show.
    // The old {1,2,1}/16 kernel put only 25% of the weight on the centre
    // sample, which flattened almost every local feature: hills read as
    // ramps and the biggest step between two neighbouring columns was 2.
    // {1,4,1}/36 keeps the same shape (it is still separable and still
    // removes 1-block terracing) while letting the short-wavelength relief
    // survive into the final height.
    static const double k[3] = {1.0, 4.0, 1.0};
    double sum = 0.0;
    for (int a = 0; a < 3; ++a)
        for (int b = 0; b < 3; ++b)
            sum += k[a] * k[b] * raw[a][b];
    int h = (int)std::lround(sum / 36.0);
    return std::clamp(h, 8, 124);
}

int columnHeight(const TerrainNoise& n, double wx, double wz) {
    double raw[3][3];
    for (int a = 0; a < 3; ++a)
        for (int b = 0; b < 3; ++b)
            raw[a][b] = rawColumnHeight(n, wx + a - 1.0, wz + b - 1.0);
    return heightFromRaw(raw);
}

namespace {

// Shared river field: the zero contour of a low-frequency fbm. Both the
// narrow channel mask and the wide valley mask read the SAME field, so the
// water always sits at the bottom of the valley that was carved for it.
inline double riverField(const TerrainNoise& n, double wx, double wz) {
    return mcp_simplex_fbm2d(n.river, wx * 0.0013, wz * 0.0013, 2, 0.5, 2.0);
}

} // namespace

double riverMask(const TerrainNoise& n, double wx, double wz) {
    // Rivers are the zero contour of this low-frequency field. |value|
    // is small along the contour, so it becomes a smooth channel.
    const double d = std::fabs(riverField(n, wx, wz));
    // Narrow on purpose: a vanilla river is only a few blocks wide. The old
    // kEdge of 0.165 spread the water over ~3x the width, which read as a
    // string of wide ponds rather than a river.
    constexpr double kCore = 0.030;   // fully carved channel floor
    constexpr double kEdge = 0.078;   // river bank
    if (d >= kEdge) return 0.0;
    if (d <= kCore) return 1.0;
    const double t = (d - kCore) / (kEdge - kCore);
    return 1.0 - t * t * (3.0 - 2.0 * t);
}

double riverValley(const TerrainNoise& n, double wx, double wz) {
    const double d = std::fabs(riverField(n, wx, wz));
    // Much wider than riverMask(): this is the valley the river runs along,
    // and its edges fade out over a long distance so the banks slope
    // instead of dropping off a cliff.
    constexpr double kValley = 0.40;
    if (d >= kValley) return 0.0;
    return 1.0 - smoothstep(0.0, kValley, d);
}

double riverShoulder(const TerrainNoise& n, double wx, double wz) {
    const double d = std::fabs(riverField(n, wx, wz));
    // A very wide, shallow skirt around the valley. One wide valley on its
    // own was measured to make the banks STEEPER, not gentler: widening the
    // mask just moved the same drop further out, so the step from valley
    // floor to untouched ground stayed abrupt. Adding this second, much
    // broader and shallower tier on top spreads the remaining drop over a
    // long distance, which is what actually flattens the transition.
    constexpr double kShoulder = 0.78;
    if (d >= kShoulder) return 0.0;
    return 1.0 - smoothstep(0.0, kShoulder, d);
}

bool isShore(const TerrainNoise& n, double wx, double wz, int h, int radius) {
    // radius defaults to 2 (see header): a 2-3 block wide band of sand at the
    // water line, which is what a vanilla beach actually is.
    // Only land within a couple of blocks of the water line can be a beach.
    // Everything else short-circuits, which keeps this cheap: it is called
    // per column but the expensive neighbour sampling only runs on the
    // handful of columns that are actually near the water.
    // Only land within a couple of blocks of the water line can be a beach.
    // The window is deliberately tight: a wide one turns every low-lying
    // plain into a beach, because in flat coastal country plenty of ground
    // sits at that height. Vanilla beaches are a narrow band right at the
    // water's edge.
    if (h < kSeaLevel - 1 || h > kSeaLevel + 2) return false;
    // Neighbour test uses the RAW height rather than the smoothed
    // columnHeight(): this predicate runs for every near-water column
    // (and again for every tree candidate), and columnHeight() costs nine
    // raw evaluations each. Asking "is this column below the water line?"
    // does not need the blur — a half-block margin absorbs it, and the
    // result is identical for every practical purpose at nine times the
    // speed.
    for (int dx = -radius; dx <= radius; ++dx) {
        for (int dz = -radius; dz <= radius; ++dz) {
            if (dx == 0 && dz == 0) continue;
            if (rawColumnHeight(n, wx + dx, wz + dz) < (double)kSeaLevel - 0.5)
                return true;
        }
    }
    return false;
}

double gravelPatch(const TerrainNoise& n, double wx, double wz, int h) {
    // Rare, clustered patches. Vanilla lays gravel on riverbeds, on
    // coastlines and on the odd inland patch; the water-proximity term
    // keeps most of them where you would actually expect gravel. `h` is
    // passed in rather than recomputed, since this runs per land column.
    const double g = mcp_simplex_fbm2d(n.surface, wx * 0.035 + 61.0,
                                       wz * 0.035 - 17.0, 2, 0.5, 2.0);
    const double nearWater = 1.0 - smoothstep(0.0, 7.0,
                                              std::fabs((double)h - (double)kSeaLevel));
    const double near = std::max(riverValley(n, wx, wz),
                                 std::max(riverMask(n, wx, wz), nearWater));
    // Threshold tuned so patches are genuinely occasional rather than a
    // second biome painted over the first.
    constexpr double kEdge = 0.70;
    if (g <= 0.46) return 0.0;
    const double t = (g - 0.46) / (kEdge - 0.46);
    return std::clamp(t * t * (3.0 - 2.0 * t) * (0.35 + 0.65 * near), 0.0, 1.0);
}

double treeFreeMask(const TerrainNoise& n, double wx, double wz, int h) {
    // Rare open ground: a low-frequency field thresholded hard, so most of
    // the world keeps its full tree cover and only occasional pockets come
    // out bare.
    const double f = mcp_simplex_fbm2d(n.surface, wx * 0.012 - 421.0,
                                       wz * 0.012 + 88.0, 2, 0.5, 2.0);
    if (f <= 0.58) return 0.0;
    const double t = std::clamp((f - 0.58) / 0.16, 0.0, 1.0);
    // Biased TOWARDS river banks and lowland (which is what plains mostly
    // are): the clearing is likeliest right next to the water, not out in
    // the middle of a forest far from it.
    // `h` is passed in by the caller, which has already computed it.
    const double lowland = 1.0 - smoothstep(4.0, 22.0,
                                            (double)h - (double)kSeaLevel);
    const double bias = std::min(1.0, 0.45 + 0.55 * std::max(riverValley(n, wx, wz),
                                                             lowland * 0.6));
    return t * t * (3.0 - 2.0 * t) * bias;
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

    // Beach: a land column at/near sea level that has open water within a
    // few columns. Uses the same isShore() the surface fill uses, so the
    // biome and the sand on the ground can never disagree.
    if (isShore(n, wx, wz, h)) return BIOME_BEACH;
    return b;
}

} // namespace cursestone::level::generator
