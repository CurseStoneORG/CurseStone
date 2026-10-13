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
#include "EmptyGenerator.h"
#include "world/format/Chunk.h"
namespace cursestone::world::generator {
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
