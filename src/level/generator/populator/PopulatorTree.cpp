#include "PopulatorTree.h"
#include "PopulatorUtil.h"
#include "level/generator/biome/BiomeRegistry.h"

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
} // namespace

void PopulatorTree::populate(GenerationContext& ctx) {
    GenRandom rng = chunkRandom(ctx.seed, ctx.chunk_x, ctx.chunk_z, 0x5EEDCAFE);

    // 16 candidate slots per chunk; each slot only plants a tree if the
    // biome under it wants trees (density comes from BiomeConfig).
    for (int i = 0; i < 16; ++i) {
        int x = (int)rng.nextInt(16);
        int z = (int)rng.nextInt(16);
        uint8_t biome = mcp_chunk_get_biome(ctx.chunk, x, z);
        const auto& cfg = biome::BiomeRegistry::forId(biome);
        if (cfg.trees.empty() || cfg.treeAmount <= 0) continue;
        if (rng.nextInt(16) >= cfg.treeAmount) continue;

        int y = surfaceY(ctx.chunk, x, z);
        if (y < 1 || y > 118) continue;

        uint8_t ground = mcp_chunk_get_block_id(ctx.chunk, x, y, z);
        if (ground == (uint8_t)block::BlockId::SNOW_BLOCK ||
            ground == (uint8_t)block::BlockId::ICE) {
            continue;   // no trees straight on ice/snow blocks
        }

        object::TreeType t = pickSpecies(cfg.trees, rng);
        object::TreeGenerator::place(ctx.chunk, x, y + 1, z, t, rng);
    }
}
} // namespace cursestone::level::generator::populator
