#include "PopulatorCave.h"
#include "level/generator/noise/SimplexNoise.h"
#include "level/format/Chunk.h"
#include <cmath>
namespace cenisyspe::level::generator::populator {
void PopulatorCave::populate(GenerationContext& ctx) {
    mcp_simplex_t* sn = mcp_simplex_new(ctx.seed ^ 0xCAFE1234);
    for (int i = 0; i < attempts_; ++i) {
        uint32_t h = mcp_simplex_hash(ctx.seed ^ 0xCAFECAFE, ctx.chunk_x * 13 + i, ctx.chunk_z * 17 + i);
        int bx = (int)(h % 16);
        int by = 10 + (int)((h >> 4) % 40);
        int bz = (int)((h >> 8) % 16);
        int len = 8 + (int)((h >> 12) % 12);
        for (int t = 0; t < len; ++t) {
            double angle = mcp_simplex_noise2d(sn, ctx.chunk_x + i * 0.5 + t * 0.1, ctx.chunk_z + i * 0.7) * 3.14159;
            bx += (int)(cos(angle) * 1.2);
            bz += (int)(sin(angle) * 1.2);
            by += (int)(mcp_simplex_noise2d(sn, t * 0.3, i * 0.5) * 1.2);
            for (int dy = -2; dy <= 2; ++dy)
            for (int dx = -2; dx <= 2; ++dx)
            for (int dz = -2; dz <= 2; ++dz) {
                if (dx*dx + dy*dy + dz*dz > 4) continue;
                int x = bx + dx, y = by + dy, z = bz + dz;
                if (x < 0 || x >= 16 || y < 0 || y >= 256 || z < 0 || z >= 16) continue;
                uint8_t blk = mcp_chunk_get_block_id(ctx.chunk, x, y, z);
                if (blk != 0 && blk != 7) mcp_chunk_set_block_id(ctx.chunk, x, y, z, 0);
            }
        }
    }
    mcp_simplex_destroy(sn);
    ctx.chunk->dirty = true;
}
} // namespace
