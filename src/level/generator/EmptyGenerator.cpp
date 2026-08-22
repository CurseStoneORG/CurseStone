#include "EmptyGenerator.h"
#include "level/format/Chunk.h"
namespace mcppeserver::level::generator {
void EmptyGenerator::generate(GenerationContext& ctx) {
    mcp_chunk_t* c = ctx.chunk;
    mcp_chunk_section_t* s = mcp_chunk_get_or_alloc_section(c, 0);
    if (s && s->blocks) {
        for (int x = 0; x < 16; ++x)
        for (int z = 0; z < 16; ++z) {
            int idx = ((0 & 15) << 8) | ((z & 15) << 4) | (x & 15);
            s->blocks[idx] = 7; // bedrock
        }
    }
    c->terrain_populated = true;
    c->light_populated  = true;
    c->dirty = true;
}
} // namespace
