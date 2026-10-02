#include "PopulatorFlower.h"
#include "PopulatorUtil.h"
#include "level/generator/object/ObjectDecorators.h"
#include "level/generator/biome/Biome.h"
using namespace cursestone::level::generator::biome;

namespace cursestone::level::generator::populator {
void PopulatorFlower::populate(GenerationContext& ctx) {
    GenRandom rng = chunkRandom(ctx.seed, ctx.chunk_x, ctx.chunk_z,
                                0xF10C0FFEE ^ (uint64_t)flower_);
    for (int i = 0; i < attempts_; ++i) {
        int x = (int)rng.nextInt(16);
        int z = (int)rng.nextInt(16);
        uint8_t biome = mcp_chunk_get_biome(ctx.chunk, x, z);
        if (!biomeHasVegetation(biome)) continue;
        int y = surfaceY(ctx.chunk, x, z);
        if (y < 0) continue;
        object::ObjectDecorators::flower(ctx.chunk, x, y, z, flower_);
    }
    ctx.chunk->dirty = true;
}
} // namespace cursestone::level::generator::populator
