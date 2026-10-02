#include "PopulatorTree.h"
#include "PopulatorUtil.h"
#include "level/generator/TerrainShape.h"
#include "level/generator/biome/BiomeRegistry.h"
#include "block/BlockIds.h"
#include <algorithm>

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

// Plants every tree that chunk (nx,nz) wants, writing into `ctx.chunk` —
// whose coordinates are (nx*16 + lx - cbx, nz*16 + lz - cbz), clipped to
// the target chunk by the tree writer. Running this for the target chunk's
// own chunk gives its trees; running it for the 8 neighbours gives the
// overlapping part of *their* trees, which is what keeps a tree that
// crosses a border from being cut in half.
void plantChunkTrees(const TerrainNoise& noise, GenerationContext& ctx, int attempts,
                     int nx, int nz, int cbx, int cbz) {
    GenRandom rng = chunkRandom(ctx.seed, nx, nz, 0x5EEDCAFE);

    // 16 candidate slots per chunk; each slot only plants a tree if the
    // biome under it wants trees (density comes from BiomeConfig).
    for (int i = 0; i < attempts; ++i) {
        const int lx = (int)rng.nextInt(16);
        const int lz = (int)rng.nextInt(16);
        const int wx = nx * 16 + lx;
        const int wz = nz * 16 + lz;

        // Trunk base Y and biome are pure functions of the world column, so
        // every chunk that renders this tree agrees on where it goes.
        const int h = columnHeight(noise, wx, wz);
        if (h < 2 || h > 119) continue;

        const uint8_t biome = columnBiome(noise, wx, wz, h);
        const auto& cfg = biome::BiomeRegistry::forId(biome);
        if (cfg.trees.empty() || cfg.treeAmount <= 0) continue;
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

        const uint8_t ground = cfg.topBlock;
        if (ground == (uint8_t)block::BlockId::SNOW_BLOCK ||
            ground == (uint8_t)block::BlockId::ICE) {
            continue;   // no trees straight on ice/snow blocks
        }

        object::TreeType t = pickSpecies(cfg.trees, rng);
        object::TreeGenerator::place(ctx.chunk, wx - cbx, h, wz - cbz, t, ground, rng);
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
