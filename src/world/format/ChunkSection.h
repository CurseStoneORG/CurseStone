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
// ChunkSection — standalone helpers for a single 16×16×16 sub-chunk.
//
// A `mcp_chunk_section_t` is the canonical sub-chunk: 4096 bytes of
// block IDs, plus three 2048-byte nibble arrays for block data,
// sky light, and block light. Chunk.h includes this header so
// Chunk can stack 16 of these on Y.
//
// Pure C, no global state. Small index/nibble helpers are inlined;
// larger allocators live in ChunkSection.c.
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Opaque struct — the layout is defined in ChunkSection.c.
// `mcp_chunk_section_t` is the public alias; users go through
// the accessors below and never touch the fields directly.
typedef struct mcp_chunk_section {
    uint8_t  y_index;            // 0..15 — which sub-Y this is.
    uint8_t* blocks;             // 4096 bytes (XZY order)
    uint8_t* data;               // 2048 bytes  (4-bit nibbles)
    uint8_t* sky_light;          // 2048 bytes  (4-bit nibbles)
    uint8_t* block_light;        // 2048 bytes  (4-bit nibbles)
    bool     non_air_top;        // true if upper half has any non-air block
} mcp_chunk_section_t;

// XZY coordinate packing: idx = (x&15) | ((z&15)<<4) | ((y&15)<<8).
// Range 0..4095 for blocks, 0..2047 for nibble arrays.
static inline uint32_t mcp_section_block_index(int x, int y, int z) {
    return (uint32_t)((x & 15) | ((z & 15) << 4) | ((y & 15) << 8));
}

static inline uint32_t mcp_section_nibble_index(int x, int y, int z) {
    return (uint32_t)((x & 15) | ((z & 15) << 4) | ((y & 15) << 7));
}

static inline uint8_t mcp_section_get_nibble(const uint8_t* arr,
                                                int x, int y, int z) {
    if (!arr) return 0;
    uint32_t i = mcp_section_nibble_index(x, y, z);
    uint8_t  b = arr[i >> 1];
    return (i & 1) ? (uint8_t)((b >> 4) & 0x0F)
                  : (uint8_t)(b & 0x0F);
}

static inline void mcp_section_set_nibble(uint8_t* arr,
                                            int x, int y, int z, uint8_t v) {
    if (!arr) return;
    v &= 0x0F;
    uint32_t i = mcp_section_nibble_index(x, y, z);
    uint8_t* b = &arr[i >> 1];
    if (i & 1) {
        *b = (uint8_t)((*b & 0x0F) | (v << 4));
    } else {
        *b = (uint8_t)((*b & 0xF0) | v);
    }
}

// Lifecycle. mcp_section_new allocates + zeroes the section, with
// sky_light defaulting to 0xFF (full bright) for freshly generated
// chunks that haven't had a light pass yet.
mcp_chunk_section_t* mcp_section_new(uint8_t y);
void                mcp_section_destroy(mcp_chunk_section_t* s);

// Fill `s->blocks` with `id` for the entire 4096-byte array.
// Used by the Flat generator and the Void generator's bedrock floor.
void mcp_section_fill_blocks(mcp_chunk_section_t* s, uint8_t id);

// True if the section has no non-air block anywhere (s == NULL or
// every byte of `blocks` is zero). Used by the network serializer
// to decide whether to skip the section.
bool mcp_section_is_empty(const mcp_chunk_section_t* s);

// Count non-air blocks in the section (debug + light-populated flag).
size_t mcp_section_count_non_air(const mcp_chunk_section_t* s);

#ifdef __cplusplus
} // extern "C"
#endif
