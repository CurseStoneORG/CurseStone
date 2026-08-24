#include "OreGenerator.h"
#include "level/format/Chunk.h"
#include "level/generator/noise/SimplexNoise.h"
namespace atomikpe::level::generator::object {
int OreGenerator::place(mcp_chunk_t* c, int64_t seed, const OreVein& v) {
    uint32_t h = mcp_simplex_hash(seed ^ ((int64_t)v.block_id << 32), c->x, c->z);
    if (h % (uint32_t)v.rarity != 0) return 0;
    uint64_t state = (uint64_t)h;
    auto next = [&]() -> uint64_t {
        state = (state ^ (state >> 30)) * 0xbf58476d1ce4e5b9ULL;
        state = (state ^ (state >> 27)) * 0x94d049bb133111ebULL;
        state ^= state >> 31;
        return state;
    };
    int bx = (int)(next() % 16);
    int by = (int)(v.min_y + (next() % (uint64_t)(v.max_y - v.min_y + 1)));
    int bz = (int)(next() % 16);
    int placed = 0;
    for (int i = 0; i < v.vein_size; ++i) {
        int dx = (int)(next() % 3) - 1;
        int dy = (int)(next() % 3) - 1;
        int dz = (int)(next() % 3) - 1;
        int x = bx + dx, y = by + dy, z = bz + dz;
        if (x < 0 || x >= 16 || y < 0 || y >= 256 || z < 0 || z >= 16) continue;
        // only replace stone (1) — preserves existing terrain
        if (mcp_chunk_get_block_id(c, x, y, z) == 1) {
            mcp_chunk_set_block_full(c, x, y, z, v.block_id, 0);
            ++placed;
        }
    }
    if (placed) c->dirty = true;
    return placed;
}
} // namespace
