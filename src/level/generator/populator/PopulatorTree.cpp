#include "PopulatorTree.h"
#include "level/generator/noise/SimplexNoise.h"
namespace mcppeserver::level::generator::populator {
void PopulatorTree::populate(GenerationContext& ctx) {
    uint64_t state = (uint64_t)mcp_simplex_hash(ctx.seed ^ 0x5EEDCAFE, ctx.chunk_x, ctx.chunk_z);
    auto next = [&]() -> uint64_t {
        state = (state ^ (state >> 30)) * 0xbf58476d1ce4e5b9ULL;
        state = (state ^ (state >> 27)) * 0x94d049bb133111ebULL;
        state ^= state >> 31;
        return state;
    };
    for (int i = 0; i < attempts_; ++i) {
        int x = (int)(next() % 16);
        int z = (int)(next() % 16);
        // find ground y by scanning column top-down
        int y = 255;
        for (; y > 0; --y) {
            int sy = y >> 4;
            if (!ctx.chunk->sections[sy]) { y -= 16; continue; }
            int idx = ((y & 15) << 8) | ((z & 15) << 4) | (x & 15);
            if (ctx.chunk->sections[sy]->blocks[idx] != 0) break;
        }
        if (y <= 1) continue;
        object::TreeGenerator::place(ctx.chunk, x, y + 1, z, type_);
    }
}
} // namespace
