#include "TreeGenerator.h"
#include <cstdlib>
#include "level/format/Chunk.h"
namespace mcppeserver::level::generator::object {
bool TreeGenerator::place(mcp_chunk_t* c, int x, int y, int z, TreeType t) {
    if (x < 0 || x >= 16 || z < 0 || z >= 16) return false;
    if (y < 0 || y >= 256 - 7) return false;
    int height = 4 + (t == TREE_SPRUCE ? 2 : 0);
    uint8_t log_id = 17, leaf_id = 18;
    for (int i = 0; i < height; ++i) mcp_chunk_set_block_id(c, x, y + i, z, log_id);
    for (int ly = height - 2; ly <= height + 1; ++ly) {
        int radius = (ly <= height - 1) ? 2 : 1;
        for (int dx = -radius; dx <= radius; ++dx)
        for (int dz = -radius; dz <= radius; ++dz) {
            if (dx == 0 && dz == 0 && ly < height) continue;
            if (abs(dx) == radius && abs(dz) == radius && (ly == height + 1)) continue;
            int bx = x + dx, bz = z + dz;
            if (bx < 0 || bx >= 16 || bz < 0 || bz >= 16) continue;
            // only place leaves in air (don't overwrite logs)
            if (mcp_chunk_get_block_id(c, bx, y + ly, bz) == 0)
                mcp_chunk_set_block_id(c, bx, y + ly, bz, leaf_id);
        }
    }
    c->dirty = true;
    return true;
}
} // namespace
