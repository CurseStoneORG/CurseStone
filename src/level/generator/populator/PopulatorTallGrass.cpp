#include "PopulatorTallGrass.h"
#include "PopulatorUtil.h"
#include "level/generator/object/ObjectDecorators.h"
#include "level/generator/biome/BiomeRegistry.h"
#include "level/generator/biome/Biome.h"
using namespace cursestone::level::generator::biome;

namespace cursestone::level::generator::populator {

namespace {
// Taiga/ice/hills grow ferns, everything else tall grass.
bool wantsFern(uint8_t b) {
    return b == BIOME_TAIGA || b == BIOME_TAIGA_HILLS || b == BIOME_ICE_PLAINS ||
           b == BIOME_ICE_MOUNTAINS || b == BIOME_EXTREME_HILLS;
}
} // namespace

void PopulatorTallGrass::populate(GenerationContext& ctx) {
    GenRandom rng = chunkRandom(ctx.seed, ctx.chunk_x, ctx.chunk_z, 0x67726173);
    for (int i = 0; i < 16; ++i) {
        int x = (int)rng.nextInt(16);
        int z = (int)rng.nextInt(16);
        uint8_t biome = mcp_chunk_get_biome(ctx.chunk, x, z);
        const auto& cfg = BiomeRegistry::forId(biome);
        if (cfg.grassAmount <= 0) continue;
        if (rng.nextInt(16) >= cfg.grassAmount) continue;
        int y = surfaceY(ctx.chunk, x, z);
        if (y < 0) continue;
        object::ObjectDecorators::tallGrass(ctx.chunk, x, y, z, wantsFern(biome) ? 2 : 1);
    }
    ctx.chunk->dirty = true;
}
} // namespace cursestone::level::generator::populator
