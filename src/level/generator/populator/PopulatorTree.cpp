#include "PopulatorTree.h"
#include "PopulatorUtil.h"
#include "level/generator/TerrainShape.h"
#include "level/generator/biome/BiomeRegistry.h"
#include "level/generator/biome/Biome.h"
#include "block/BlockIds.h"
#include <algorithm>
#include <utility>
#include <vector>

namespace cursestone::level::generator::populator {

namespace {

// Weighted species pick from a biome's tree list.
object::TreeType pickSpecies(const std::vector<biome::TreeSpawn>& trees, GenRandom& rng) {
    int total = 0;
    for (const auto& t : trees) total += t.weight;
    if (total <= 0) return object::TREE_OAK;
    int r = (int)rng.nextInt(total);
    for (const auto& t : trees) {
        r -= t.weight;
        if (r < 0) return t.type;
    }
    return trees.back().type;
}

// Can a tree stand on this column at all?
//
// Everything here is a pure function of (worldX, worldZ), which is what
// keeps the 3x3 rescan below in agreement: whichever chunk renders a tree
// reaches the same verdict about it.
bool columnCanHoldTree(const TerrainNoise& noise, int wx, int wz, int h,
                       uint8_t biome, uint8_t& groundOut) {
    // No trees in water. This was the worst of the old bugs: the
    // generator happily dropped oak trunks into the middle of rivers and
    // out in the ocean, standing on the river bed under the water line.
    if (h < kSeaLevel) return false;
    if (h > 119) return false;

    // Not in a river channel, and not on the water's edge either — a tree
    // rooted in the shallows reads as broken even when it is above water.
    if (riverMask(noise, wx, wz) > 0.05) return false;
    if (isShore(noise, wx, wz, h)) return false;

    const auto& cfg = biome::BiomeRegistry::forId(biome);
    if (cfg.trees.empty() || cfg.treeAmount <= 0) return false;

    const uint8_t ground = cfg.topBlock;
    if (ground == (uint8_t)block::BlockId::SNOW_BLOCK ||
        ground == (uint8_t)block::BlockId::ICE) {
        return false;   // no trees straight on ice/snow blocks
    }
    groundOut = ground;
    return true;
}

// Plant one tree of an explicit species. Callers must have already cleared
// the column with columnCanHoldTree().
void plantSpecies(GenerationContext& ctx, GenRandom& rng, int h,
                  int wx, int wz, int cbx, int cbz, object::TreeType type,
                  uint8_t ground) {
    object::TreeGenerator::place(ctx.chunk, wx - cbx, h, wz - cbz,
                                 type, ground, rng);
}

// Plant one tree, species taken from the biome's weighted list. Callers
// must have already cleared the column with columnCanHoldTree().
void plantAt(const TerrainNoise& noise, GenerationContext& ctx, GenRandom& rng,
             int wx, int wz, int cbx, int cbz, uint8_t biome, uint8_t ground) {
    const int h = columnHeight(noise, wx, wz);
    plantSpecies(ctx, rng, h, wx, wz, cbx, cbz,
                 pickSpecies(biome::BiomeRegistry::forId(biome).trees, rng),
                 ground);
}

// Biomes whose trees come from the grove pass (and ONLY from it), instead
// of the per-slot scatter of pass 2: plains, savanna and their raised
// variants.
inline bool isGroveBiome(uint8_t b) {
    return b == biome::BIOME_PLAINS || b == biome::BIOME_SAVANNA ||
           b == biome::BIOME_PLAINS_PLATEAU ||
           b == biome::BIOME_SAVANNA_PLATEAU;
}

// Trees inside one cluster of plains.
struct ClusterMember {
    int wx, wz;
};

// Every trunk that ONE source chunk (nx,nz) has already put down, in world
// coordinates.
//
// It is deliberately local to the source chunk and NOT shared across the
// whole 3x3 rescan. A tree must come out identical no matter which chunk is
// being rendered: if the spacing test could see trunks planted by a
// neighbouring source chunk, then whether a grove member was accepted would
// depend on where in the rescan loop that neighbour happened to be — and the
// same tree would then exist in one chunk and not in its neighbour.
using PlantedList = std::vector<std::pair<int, int>>;

// Is (wx,wz) within `minDist` of a trunk that is already down?
bool tooCloseToPlanted(const PlantedList& planted, int wx, int wz, int minDist) {
    const int d2 = minDist * minDist;
    for (const auto& p : planted) {
        const int dx = wx - p.first, dz = wz - p.second;
        if (dx * dx + dz * dz < d2) return true;
    }
    return false;
}

// Plants every tree that chunk (nx,nz) wants, writing into `ctx.chunk` —
// whose coordinates are (nx*16 + lx - cbx, nz*16 + lz - cbz), clipped to
// the target chunk by the tree writer. Running this for the target chunk's
// own chunk gives its trees; running it for the 8 neighbours gives the
// overlapping part of *their* trees, which is what keeps a tree that
// crosses a border from being cut in half.
void plantChunkTrees(const TerrainNoise& noise, GenerationContext& ctx, int attempts,
                     int nx, int nz, int cbx, int cbz) {
    GenRandom rng = chunkRandom(ctx.seed, nx, nz, 0x5EEDCAFE);
    // Trunks placed by THIS source chunk only (see PlantedList).
    PlantedList planted;
    planted.reserve(64);

    // ---- Pass 1: groves in plains -------------------------------------
    // Plains used to scatter lone oaks evenly over the field, which is why
    // they read as a dotted orchard rather than grassland. Instead we pick
    // a few grove centres per chunk and drop a handful of trees around
    // each one, keeping a minimum spacing so a grove looks like a grove.
    // A grove can reach outside the 16x16 it was seeded from; the tree
    // writer clips, and because every chunk re-runs the 3x3 rescan the
    // same groves come out identical everywhere.
    constexpr int kClustersPerChunk = 2;
    for (int c = 0; c < kClustersPerChunk; ++c) {
        if (rng.nextInt(1000) >= 55) continue;          // ~5.5% per cluster slot
        const int cxw = nx * 16 + (int)rng.nextInt(16);
        const int czw = nz * 16 + (int)rng.nextInt(16);

        // The grove only counts if its centre really is open grassland.
        const int ch = columnHeight(noise, cxw, czw);
        const uint8_t cBiome = columnBiome(noise, cxw, czw, ch);
        if (!isGroveBiome(cBiome)) continue;

        // 2-4 trees. The user asked for 3-4 blocks between trunks, which is
        // also roughly what vanilla does, so that is the hard minimum. It is
        // enforced against every trunk already placed in this chunk (see
        // PlantedList), not just against the ones in this grove — otherwise
        // two groves seeded in neighbouring chunks still land a block apart.
        constexpr int kMinSpacing = 4;
        const int want = 2 + (int)rng.nextInt(3);
        std::vector<ClusterMember> placed;
        placed.reserve(want);
        for (int t = 0; t < want * 6 && (int)placed.size() < want; ++t) {
            const int wx = cxw + (int)rng.nextInt(13) - 6;
            const int wz = czw + (int)rng.nextInt(13) - 6;
            bool tooClose = false;
            for (const auto& m : placed) {
                const int dx = wx - m.wx, dz = wz - m.wz;
                if (dx * dx + dz * dz < kMinSpacing * kMinSpacing) {
                    tooClose = true;
                    break;
                }
            }
            if (tooClose) continue;
            if (tooCloseToPlanted(planted, wx, wz, kMinSpacing)) continue;
            const int h = columnHeight(noise, wx, wz);
            if (h < kSeaLevel || h > 119) continue;
            if (riverMask(noise, wx, wz) > 0.05) continue;
            if (isShore(noise, wx, wz, h)) continue;
            const uint8_t b = columnBiome(noise, wx, wz, h);
            if (!isGroveBiome(b)) continue;
            uint8_t ground = 0;
            if (!columnCanHoldTree(noise, wx, wz, h, b, ground)) continue;
            // A grove on a slope is fine, but keep the ground consistent.
            plantAt(noise, ctx, rng, wx, wz, cbx, cbz, b, ground);
            planted.emplace_back(wx, wz);
            placed.push_back({wx, wz});
        }
    }

    // ---- Pass 1.5: birch copses along the rivers in the plains -------
    // Birch hugs the river banks in the grassland: common enough to be a
    // recognisable feature of a river walk, rare enough that most reaches
    // have none. Only the plains count — the woodland patches, the savanna
    // and every other biome keep their own species.
    constexpr int kRiverBirchTries = 3;
    for (int i = 0; i < kRiverBirchTries; ++i) {
        if (rng.nextInt(100) >= 32) continue;           // rare-to-common
        const int wx = nx * 16 + (int)rng.nextInt(16);
        const int wz = nz * 16 + (int)rng.nextInt(16);
        const int h = columnHeight(noise, wx, wz);
        if (h < kSeaLevel || h > 119) continue;
        // On the river bank: inside the river's valley, but out of the
        // water and off the shoreline.
        if (riverValley(noise, wx, wz) <= 0.10) continue;
        if (riverMask(noise, wx, wz) > 0.05) continue;
        if (isShore(noise, wx, wz, h)) continue;
        const uint8_t b = columnBiome(noise, wx, wz, h);
        if (b != biome::BIOME_PLAINS && b != biome::BIOME_PLAINS_PLATEAU)
            continue;
        if (tooCloseToPlanted(planted, wx, wz, 3)) continue;
        uint8_t ground = 0;
        if (!columnCanHoldTree(noise, wx, wz, h, b, ground)) continue;
        plantSpecies(ctx, rng, h, wx, wz, cbx, cbz, object::TREE_BIRCH, ground);
        planted.emplace_back(wx, wz);
    }

    // ---- Pass 2: the rest of the biomes, as before -------------------
    // Forests keep their own per-slot density; only plains were rewritten
    // above. 16 candidate slots per chunk, each accepted only if the biome
    // under it wants trees (density comes from BiomeConfig).
    for (int i = 0; i < attempts; ++i) {
        const int lx = (int)rng.nextInt(16);
        const int lz = (int)rng.nextInt(16);
        const int wx = nx * 16 + lx;
        const int wz = nz * 16 + lz;

        const int h = columnHeight(noise, wx, wz);
        if (h < 2 || h > 119) continue;

        const uint8_t biome = columnBiome(noise, wx, wz, h);
        const auto& cfg = biome::BiomeRegistry::forId(biome);
        if (cfg.trees.empty() || cfg.treeAmount <= 0) continue;

        // Plains/savanna (and their raised variants) are handled by the
        // grove pass above and ONLY by it. Letting them through here as
        // well is what kept saturating the plains: the groves were spaced
        // properly and then this pass sprinkled lone oaks on top of them at
        // 1 block apart, which is exactly the scattered look the groves
        // were meant to replace.
        if (isGroveBiome(biome)) continue;

        if (rng.nextInt(16) >= cfg.treeAmount) continue;

        // Rare open pockets: a low-frequency mask that occasionally rejects
        // a tree no matter how dense the biome is, so river banks and plains
        // get occasional BARE clearings instead of just thinner forest. The
        // probability is ramped with the mask so the core of a clearing ends
        // up effectively treeless — using the raw mask value only halved the
        // tree density inside it, which is thinner woods, not a clearing.
        const double bare = treeFreeMask(noise, wx, wz, h);
        if (bare > 0.0) {
            const double p = std::min(1.0, bare * 1.7);
            if (rng.nextInt(1000) < (int)(p * 1000.0)) continue;
        }

        uint8_t ground = 0;
        if (!columnCanHoldTree(noise, wx, wz, h, biome, ground)) continue;
        plantAt(noise, ctx, rng, wx, wz, cbx, cbz, biome, ground);
        planted.emplace_back(wx, wz);
    }
}

} // namespace

void PopulatorTree::populate(GenerationContext& ctx) {
    TerrainNoise noise(ctx.seed);
    const int cbx = ctx.chunk_x * 16;
    const int cbz = ctx.chunk_z * 16;

    // Rescan the 3x3 chunk neighbourhood: the target chunk plus the eight
    // around it. A tree whose trunk is in a neighbour still has its canopy
    // and logs written here wherever they overlap this chunk, so nothing is
    // chopped at the border anymore.
    for (int nx = ctx.chunk_x - 1; nx <= ctx.chunk_x + 1; ++nx)
        for (int nz = ctx.chunk_z - 1; nz <= ctx.chunk_z + 1; ++nz)
            plantChunkTrees(noise, ctx, attempts_, nx, nz, cbx, cbz);

    ctx.chunk->dirty = true;
}

} // namespace cursestone::level::generator::populator
