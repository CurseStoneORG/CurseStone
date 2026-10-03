// SPDX-License-Identifier: LGPL-3.0-or-later
#include "Structure.h"

namespace cursestone::level::generator::structure {

size_t Structure::placeInto(mcp_chunk_t* chunk, int chunkX, int chunkZ,
                            int wx, int wy, int wz) const {
    const int bx = chunkX * 16;
    const int bz = chunkZ * 16;
    size_t written = 0;

    for (int ly = 0; ly < sizeY_; ++ly) {
        // MCPE 0.15.x worlds are 128 tall, and a structure that would poke
        // out of the top is clipped rather than half written; callers
        // already refuse those placements outright.
        const int y = wy + ly;
        if (y < 0 || y > 127) continue;

        for (int lz = 0; lz < sizeZ_; ++lz) {
            const int cz = wz + lz - bz;
            if (cz < 0 || cz > 15) continue;

            for (int lx = 0; lx < sizeX_; ++lx) {
                const int cx = wx + lx - bx;
                if (cx < 0 || cx > 15) continue;
                const BlockState b = blockAt(lx, ly, lz);
                mcp_chunk_set_block_full(chunk, cx, y, cz, b.id, b.data);
                ++written;
            }
        }
    }
    return written;
}

} // namespace cursestone::level::generator::structure