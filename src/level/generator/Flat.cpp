#include "Flat.h"
#include "level/format/Chunk.h"
#include <string.h>
namespace mcppeserver::level::generator {
void Flat::generate(GenerationContext& ctx) {
    mcp_chunk_t* c = ctx.chunk;
    int y = 0;
    for (const auto& L : layers_) {
        for (int k = 0; k < L.count && y < 256; ++k, ++y) {
            uint8_t sy = (uint8_t)(y >> 4);
            mcp_chunk_section_t* s = mcp_chunk_get_or_alloc_section(c, sy);
            if (!s) continue;
            // fill the 4096-byte blocks array directly for speed
            if (s->blocks) {
                for (int x = 0; x < 16; ++x)
                for (int z = 0; z < 16; ++z) {
                    int idx = ((y & 15) << 8) | ((z & 15) << 4) | (x & 15);
                    s->blocks[idx] = L.block_id;
                }
            }
        }
    }
    for (int x = 0; x < 16; ++x)
    for (int z = 0; z < 16; ++z) mcp_chunk_set_biome(c, x, z, 1);
    for (int x = 0; x < 16; ++x)
    for (int z = 0; z < 16; ++z) mcp_chunk_set_height_map(c, x, z, y);
    c->terrain_populated = true;
    c->light_populated  = true;
    c->dirty = true;
}
} // namespace
