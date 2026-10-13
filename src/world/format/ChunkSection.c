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
// SPDX-License-Identifier: LGPL-3.0-or-later
// ChunkSection — implementation of standalone sub-chunk helpers.
//
// The struct layout lives in ChunkSection.h. Here we provide the
// allocator + bulk operations. See Chunk.c for the per-block access
// that operates on a full Chunk (16 sections + biome + heightmap).
#include "ChunkSection.h"

#include <stdlib.h>
#include <string.h>

mcp_chunk_section_t* mcp_section_new(uint8_t y) {
    mcp_chunk_section_t* s = (mcp_chunk_section_t*)calloc(1, sizeof(*s));
    if (!s) return NULL;
    s->y_index     = y;
    s->blocks      = (uint8_t*)calloc(4096, 1);
    s->data        = (uint8_t*)calloc(2048, 1);
    s->sky_light   = (uint8_t*)calloc(2048, 1);
    s->block_light = (uint8_t*)calloc(2048, 1);
    // Default sky_light to full-bright (15) so freshly generated chunks
    // aren't pitch black before the light pass runs.
    if (s->sky_light) {
        memset(s->sky_light, 0xFF, 2048);
    }
    s->non_air_top = false;
    // Allocation failure mid-way: free what was allocated and bail.
    if (!s->blocks || !s->data || !s->sky_light || !s->block_light) {
        free(s->blocks);
        free(s->data);
        free(s->sky_light);
        free(s->block_light);
        free(s);
        return NULL;
    }
    return s;
}

void mcp_section_destroy(mcp_chunk_section_t* s) {
    if (!s) return;
    free(s->blocks);
    free(s->data);
    free(s->sky_light);
    free(s->block_light);
    free(s);
}

void mcp_section_fill_blocks(mcp_chunk_section_t* s, uint8_t id) {
    if (!s || !s->blocks) return;
    memset(s->blocks, id, 4096);
    s->non_air_top = (id != 0);
}

bool mcp_section_is_empty(const mcp_chunk_section_t* s) {
    if (!s || !s->blocks) return true;
    for (size_t i = 0; i < 4096; ++i) {
        if (s->blocks[i] != 0) return false;
    }
    return true;
}

size_t mcp_section_count_non_air(const mcp_chunk_section_t* s) {
    if (!s || !s->blocks) return 0;
    size_t n = 0;
    for (size_t i = 0; i < 4096; ++i) {
        if (s->blocks[i] != 0) ++n;
    }
    return n;
}
