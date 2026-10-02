#include "PopulatorTallGrass.h"
#include "PopulatorUtil.h"
#include "level/generator/object/ObjectDecorators.h"
#include "level/generator/biome/Biome.h"
using namespace cursestone::level::generator::biome;

namespace cursestone::level::generator::populator {
void PopulatorTallGrass::populate(GenerationContext& ctx) {
    GenRandom rng = chunkRandom(ctx.seed, ctx.chunk_x, ctx.chunk_z, 0x67726173);
    for (int i = 0; i < attempts_; ++i) {
        int x = (int)rng.nextInt(16);
        int z = (int)rng.nextInt(16);
        uint8_t biome = mcp_chunk_get_biome(ctx.chunk, x, z);
        if (!biomeHasVegetation(biome)) continue;
        int y = surfaceY(ctx.chunk, x, z);
        if (y < 0) continue;
        // Taiga/cold biomes grow ferns (data 2); the rest grow tall grass.
        uint8_t data = (biome == BIOME_TAIGA || biome == BIOME_TAIGA_HILLS ||
                        biome == BIOME_EXTREME_HILLS) ? 2 : 1;
        object::ObjectDecorators::tallGrass(ctx.chunk, x, y, z, data);
    }
    ctx.chunk->dirty = true;
}
} // namespace cursestone::level::generator::populator
