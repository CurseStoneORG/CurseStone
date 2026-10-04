#include "Nether.h"
#include <cmath>
namespace cursestone::level::generator {
Nether::Nether(int64_t seed) : seed_(seed) {
    terrain_ = mcp_simplex_new(seed);
    cave_ = mcp_simplex_new(seed ^ 0xDEADBEEF);
}
Nether::~Nether() {
    mcp_simplex_destroy(terrain_);
    mcp_simplex_destroy(cave_);
}
void Nether::generate(GenerationContext& ctx) {
    mcp_chunk_t* c = ctx.chunk;
    for (int x = 0; x < 16; ++x)
    for (int z = 0; z < 16; ++z) {
        double wx = (ctx.chunk_x * 16 + x) * 0.03;
        double wz = (ctx.chunk_z * 16 + z) * 0.03;
        double n = mcp_simplex_noise2d(terrain_, wx, wz);
        int h = (int)(64 + n * 24);  // 40..88
        for (int y = 0; y < h && y < 128; ++y) {
            int sy = y >> 4;
            if (!c->sections[sy]) c->sections[sy] = mcp_section_new((uint8_t)sy);
            if (!c->sections[sy]) continue;
            int idx = ((y & 15) << 8) | ((z & 15) << 4) | (x & 15);
            c->sections[sy]->blocks[idx] = 87;  // netherrack
        }
        // lava lake at y<12
        for (int y = 0; y < 12; ++y) {
            int sy = y >> 4;
            if (!c->sections[sy]) continue;
            int idx = ((y & 15) << 8) | ((z & 15) << 4) | (x & 15);
            if (c->sections[sy]->blocks[idx] == 0) c->sections[sy]->blocks[idx] = 10; // lava
        }
        if (c->height_map) c->height_map[x * 16 + z] = h;
    }
    // carve caves with 3D noise
    for (int y = 0; y < 128; ++y)
    for (int x = 0; x < 16; ++x)
    for (int z = 0; z < 16; ++z) {
        double c_n = mcp_simplex_noise3d(cave_, (ctx.chunk_x * 16 + x) * 0.05, y * 0.05, (ctx.chunk_z * 16 + z) * 0.05);
        if (c_n > 0.6) {
            int sy = y >> 4;
            if (!c->sections[sy]) continue;
            int idx = ((y & 15) << 8) | ((z & 15) << 4) | (x & 15);
            c->sections[sy]->blocks[idx] = 0;
        }
    }
    c->terrain_populated = true;
    c->light_populated = true;
    c->dirty = true;
}
void Nether::populate(GenerationContext&) {
    // no surface features in nether
}
} // namespace
