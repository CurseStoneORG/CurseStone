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
    mcp_simplex_t* height  = nullptr;  // continents + hills + ridges + detail
    mcp_simplex_t* temp    = nullptr;  // climate: temperature
    mcp_simplex_t* humid   = nullptr;  // climate: humidity
    mcp_simplex_t* river   = nullptr;  // river network (value crosses 0)
    mcp_simplex_t* surface = nullptr;  // gravel patches + bare (tree-less) ground

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
// This is the NARROW mask — it decides where the water actually is, so it
// is deliberately thin: a vanilla river is a few blocks wide, not a lake.
// Rivers follow the zero contour of the river noise, so they form a
// connected network that cuts through hills and mountains.
double riverMask(const TerrainNoise& n, double wx, double wz);

// Distance in BLOCKS from the river's centre line, estimated as
// |field| / |gradient|. This is the coordinate the whole river shape is
// built on: the terrain carve, the biome choice, the shoreline test and
// the bank all read it, so they can never disagree about where the river
// is or how wide it is.
double riverDistance(const TerrainNoise& n, double wx, double wz);

// Wide, gentle mask in [0,1] shaping the VALLEY the river runs through.
// This is deliberately much broader than riverMask(): the water sits in a
// thin channel at the bottom of a wide, softly sloping valley, which is
// what vanilla does. Keeping the two separate is what stops the river from
// becoming a deep slot canyon with vertical walls.
double riverValley(const TerrainNoise& n, double wx, double wz);

// Even wider and much shallower skirt around riverValley(). Together the two
// carve a valley with a long, gentle approach instead of a step down to the
// water — a single wide valley measurably made the banks steeper, because
// widening the mask only pushes the same drop further from the centre.
double riverShoulder(const TerrainNoise& n, double wx, double wz);

// True when this column is a shoreline: at (or within a couple of blocks
// of) the water level AND within `radius` columns of open water.
bool isShore(const TerrainNoise& n, double wx, double wz, int h, int radius = 2);

// Same test, but the water it touches must be OCEAN and not a river
// channel. This is what separates the two very different cases the
// reference generator makes: sandy beaches are the NORM along the sea,
// while a river bank is normally grass and only occasionally sand.
bool isOceanShore(const TerrainNoise& n, double wx, double wz, int h, int radius = 2);

// Rare but LONG sand banks along rivers, in [0,1]. Deliberately built from
// a very low-frequency field so that when it does fire it runs alongside
// the river for hundreds of blocks instead of making round patches, and it
// is gated on the river valley so it can only ever appear next to water.
double riverSandBank(const TerrainNoise& n, double wx, double wz);

// Continent field (the long-wavelength ocean/land shape), exposed so the
// biome choice and the height field can agree on where the coast is.
double continentField(const TerrainNoise& n, double wx, double wz);

// How "plain" this column is, in [0,1]. Broad, flat regions that are
// strongly biased toward coastal lowland, so the big open grassland sits
// against the sea the way the reference does. The terrain amplitude and
// the biome choice both read this, which is what keeps "plains" meaning
// the same flat thing in both.
double plainsFactor(const TerrainNoise& n, double wx, double wz);

// Climate at a world column, in [-1, 1]. Exposed so the terrain and the
// biome selection read the exact same temperature/humidity (the savanna
// flattening below has to agree with BiomeRegistry::select).
double tempAt(const TerrainNoise& n, double wx, double wz);
double humidAt(const TerrainNoise& n, double wx, double wz);

// ---- Plains variant -----------------------------------------------------
// Raised plains variant gate in [0,1]: broad patches that stand a few
// blocks ABOVE the surrounding plains and stay flat on top. Already gated
// on plainsFactor, so it can only ever fire on actual grassland.
double plainsPlateau(const TerrainNoise& n, double wx, double wz);

// Forest patches inside the plains, in [0,1]. Broad woodland-scale
// patches that turn open grassland into a patch of forest (the biome
// selection uses the same mask, so the trees and the biome agree).
double plainsForest(const TerrainNoise& n, double wx, double wz);

// ---- Savanna -----------------------------------------------------------
// How strongly this column is a savanna PLAIN, in [0,1]: warm and
// moderately dry (never as dry as the desert, never as wet as the jungle)
// and inside a broad patch mask, so savanna comes in long stretches of
// low-relief ground instead of a thin climate stripe.
double savannaFactor(const TerrainNoise& n, double wx, double wz);

// Savanna canyon gate in [0,1]: a broad raised table ~25 blocks above the
// savanna plain, with steep canyon walls. Gated hard on the CORE of a
// savanna (savannaFactor), so it can never leak into plains, desert or
// jungle, and its own mask is thresholded sharply so the transition to the
// surrounding savanna reads as a canyon wall.
double savannaCanyon(const TerrainNoise& n, double wx, double wz);

// Gravel patch mask in [0,1]: rare patches of gravel on the surface,
// clustered around rivers and coastlines the way vanilla scatters them.
double gravelPatch(const TerrainNoise& n, double wx, double wz, int h);

// Tree-free mask in [0,1]: rare open patches where no tree grows, biased
// towards river banks and plains. 1 = guaranteed no tree, 0 = unaffected.
double treeFreeMask(const TerrainNoise& n, double wx, double wz, int h);

// Biome id for a world column, given its height. Pure function of the
// world coordinates, matching what Normal::generate stores.
uint8_t columnBiome(const TerrainNoise& n, double wx, double wz, int h);

} // namespace cursestone::level::generator
