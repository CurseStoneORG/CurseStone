#include "PopulatorFlower.h"
#include "level/generator/noise/SimplexNoise.h"
#include "level/format/Chunk.h"
namespace cenisyspe::level::generator::populator {
void PopulatorFlower::populate(GenerationContext& ctx) {
    uint64_t state = (uint64_t)mcp_simplex_hash(ctx.seed ^ 0xF10C0FFEE, ctx.chunk_x, ctx.chunk_z);
    auto next = [&]() -> uint64_t {
        state = (state ^ (state >> 30)) * 0xbf58476d1ce4e5b9ULL;
        state = (state ^ (state >> 27)) * 0x94d049bb133111ebULL;
        state ^= state >> 31;
        return state;
    };
    for (int i = 0; i < attempts_; ++i) {
        int x = (int)(next() % 16);
        int z = (int)(next() % 16);
        int y = 255;
        for (; y > 0; --y) {
            if (mcp_chunk_get_block_id(ctx.chunk, x, y, z) == 2) break;
        }
        if (y > 0 && y < 255 && mcp_chunk_get_block_id(ctx.chunk, x, y + 1, z) == 0)
            mcp_chunk_set_block_id(ctx.chunk, x, y + 1, z, flower_);
    }
}
} // namespace
