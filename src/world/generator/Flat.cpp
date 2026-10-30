/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

 * CurseStone | Open Source Server Software
 *
 * Copyright (C) 2026 CurseStoneTeam
 *
 * Developed by CurseStoneTeam.
 * Built for an open and extensible Minecraft server ecosystem.
 *
 * Licensed under the GNU Lesser General Public License v3.0 or later.
 * You may study, modify, and redistribute this software under its license.
 *
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 * Repository: https://github.com/CurseStoneTeam/CurseStone
 *
 */
#include "Flat.h"
#include "world/format/Chunk.h"
#include <string.h>
namespace cursestone::world::generator {
void Flat::generate(GenerationContext& ctx) {
    mcp_chunk_t* c = ctx.chunk;
    int y = 0;
    for (const auto& L : layers_) {
        for (int k = 0; k < L.count && y < 256; ++k, ++y) {
            uint8_t sy = (uint8_t)(y >> 4);
            mcp_chunk_section_t* s = mcp_chunk_get_or_alloc_section(c, sy);
            if (!s)
                continue;
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
        for (int z = 0; z < 16; ++z)
            mcp_chunk_set_biome(c, x, z, 1);
    // Flat knows exactly where its surface ends, so setting the
    // heightmap by hand stays valid. What was missing was the real light
    // pass: light_populated was flagged without computing anything,
    // leaving default sky_light (potentially all 0 = black world). Fix:
    // run mcp_chunk_propagate_sky_light, which sets light_populated
    // itself.
    for (int x = 0; x < 16; ++x)
        for (int z = 0; z < 16; ++z)
            mcp_chunk_set_height_map(c, x, z, y);
    c->terrain_populated = true;
    c->dirty = true;
    mcp_chunk_propagate_sky_light(c);
}
} // namespace
