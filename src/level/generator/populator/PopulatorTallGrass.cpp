#include "PopulatorTallGrass.h"
#include "level/generator/noise/SimplexNoise.h"
#include "level/format/Chunk.h"
namespace cursestone::level::generator::populator {
void PopulatorTallGrass::populate(GenerationContext& ctx) {
    uint64_t state = (uint64_t)mcp_simplex_hash(ctx.seed ^ 0x67726173, ctx.chunk_x, ctx.chunk_z);
    auto next = [&]() -> uint64_t {
        state = (state ^ (state >> 30)) * 0xbf58476d1ce4e5b9ULL;
        state = (state ^ (state >> 27)) * 0x94d049bb133111ebULL;
        state ^= state >> 31;
        return state;
    };
    int placed = 0;
    for (int i = 0; i < attempts_; ++i) {
        int x = (int)(next() % 16);
        int z = (int)(next() % 16);
        int y = 255;
        for (; y > 0; --y) {
            if (mcp_chunk_get_block_id(ctx.chunk, x, y, z) == 2) break; // on grass
        }
        if (y > 0 && y < 255) {
            mcp_chunk_set_block_full(ctx.chunk, x, y + 1, z, 31, 1); // tall grass
            ++placed;
        }
    }
    if (placed) ctx.chunk->dirty = true;
}
} // namespace
