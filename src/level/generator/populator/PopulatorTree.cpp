#include "PopulatorTree.h"
#include "PopulatorUtil.h"
#include "level/generator/biome/Biome.h"
using namespace cursestone::level::generator::biome;

namespace cursestone::level::generator::populator {

namespace {
object::TreeType speciesForBiome(uint8_t b, GenRandom& rng) {
    switch (b) {
        case BIOME_PLAINS:
            return (rng.nextInt(6) == 0) ? object::TREE_BIG_OAK : object::TREE_OAK;
        case BIOME_FOREST:
        case BIOME_FOREST_HILLS: {
            int r = (int)rng.nextInt(10);
            if (r < 5) return object::TREE_OAK;
            if (r < 8) return object::TREE_BIRCH;
            return object::TREE_BIG_OAK;
        }
        case BIOME_TAIGA:
        case BIOME_TAIGA_HILLS:
        case BIOME_EXTREME_HILLS:
            return (rng.nextInt(4) == 0) ? object::TREE_OAK : object::TREE_SPRUCE;
        case BIOME_SWAMPLAND:
            return object::TREE_SWAMP;
        case BIOME_JUNGLE:
        case BIOME_JUNGLE_HILLS:
            return object::TREE_JUNGLE;
        case BIOME_SAVANNA:
        case BIOME_SAVANNA_PLATEAU:
            return object::TREE_ACACIA;
        default:
            return object::TREE_OAK;
    }
}
} // namespace

void PopulatorTree::populate(GenerationContext& ctx) {
    GenRandom rng = chunkRandom(ctx.seed, ctx.chunk_x, ctx.chunk_z, 0x5EEDCAFE);

    int count = attempts_;
    if (rng.nextInt(10) == 0) ++count;

    for (int i = 0; i < count; ++i) {
        int x = (int)rng.nextInt(16);
        int z = (int)rng.nextInt(16);
        int y = surfaceY(ctx.chunk, x, z);
        if (y < 1 || y > 120) continue;
        uint8_t biome = mcp_chunk_get_biome(ctx.chunk, x, z);
        if (!biomeHasVegetation(biome)) continue;
        // No trees on bare snow/ice/high stone.
        uint8_t ground = mcp_chunk_get_block_id(ctx.chunk, x, y, z);
        if (ground == (uint8_t)block::BlockId::SNOW_BLOCK ||
            ground == (uint8_t)block::BlockId::ICE ||
            ground == (uint8_t)block::BlockId::STONE) {
            continue;
        }
        object::TreeType t = speciesForBiome(biome, rng);
        object::TreeGenerator::place(ctx.chunk, x, y + 1, z, t, rng);
    }
}
} // namespace cursestone::level::generator::populator
