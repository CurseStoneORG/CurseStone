/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

 * CurseStone | Open Source Server Software
 *
 * Copyright (C) 2026 CurseStoneORG
 *
 * Developed by CurseStoneORG.
 * Built for an open and extensible Minecraft server ecosystem.
 *
 * Licensed under the GNU Lesser General Public License v3.0 or later.
 * You may study, modify, and redistribute this software under its license.
 *
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 * Repository: https://github.com/CurseStoneORG/CurseStone
 *
 */
// SPDX-License-Identifier: LGPL-3.0-or-later
// BinaryStream — dynamic byte buffer with read/write cursor.
// Used by RakLib packets, NBT, and the MCPE protocol packets.
// Mirrors the historical BinaryStream layout from old Aquamarine.
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <vector>


typedef struct mcp_bstream {
    uint8_t* data;
    size_t size;     // bytes used
    size_t capacity; // bytes allocated
    size_t offset;   // read cursor
    int owned;       // 1 when data is malloc'd here; 0 when borrowed/attached
    void* vec;       // attach mode: std::vector<uint8_t>* owning the storage
} mcp_bstream_t;

// Lifecycle
void mcp_bstream_init(mcp_bstream_t* s);
void mcp_bstream_init_read(mcp_bstream_t* s, const uint8_t* data, size_t len);
// Zero-copy read mode: the stream borrows `data` and never frees or writes
// through it. The caller must keep the buffer alive for the whole parse.
void mcp_bstream_init_read_borrow(mcp_bstream_t* s, const uint8_t* data, size_t len);
// Write-attach mode: every write appends straight into *vec, which owns and
// grows the storage, so the result is never copied out. mcp_bstream_free()
// releases nothing in this mode.
void mcp_bstream_attach_vec(mcp_bstream_t* s, std::vector<uint8_t>* vec);
void mcp_bstream_free(mcp_bstream_t* s);

// Reset (keeps allocation, sets size and offset to 0)

// Write helpers
void mcp_bstream_write_bytes(mcp_bstream_t* s, const void* data, size_t len);
void mcp_bstream_write_u8(mcp_bstream_t* s, uint8_t v);
void mcp_bstream_write_i8(mcp_bstream_t* s, int8_t v);
void mcp_bstream_write_u16_be(mcp_bstream_t* s, uint16_t v);
void mcp_bstream_write_u16_le(mcp_bstream_t* s, uint16_t v);
void mcp_bstream_write_u32_be(mcp_bstream_t* s, uint32_t v);
void mcp_bstream_write_u32_le(mcp_bstream_t* s, uint32_t v);
void mcp_bstream_write_u64_be(mcp_bstream_t* s, uint64_t v);
void mcp_bstream_write_u64_le(mcp_bstream_t* s, uint64_t v);
void mcp_bstream_write_f32_be(mcp_bstream_t* s, float v);
void mcp_bstream_write_f32_le(mcp_bstream_t* s, float v);
void mcp_bstream_write_f64_be(mcp_bstream_t* s, double v);
void mcp_bstream_write_f64_le(mcp_bstream_t* s, double v);
void mcp_bstream_write_var_u32(mcp_bstream_t* s, uint32_t v);
void mcp_bstream_write_var_u64(mcp_bstream_t* s, uint64_t v);

// Read helpers
size_t mcp_bstream_remaining(const mcp_bstream_t* s);
int mcp_bstream_read_bytes(mcp_bstream_t* s, void* out, size_t len);
int mcp_bstream_read_u8(mcp_bstream_t* s, uint8_t* v);
int mcp_bstream_read_i8(mcp_bstream_t* s, int8_t* v);
int mcp_bstream_read_u16_be(mcp_bstream_t* s, uint16_t* v);
int mcp_bstream_read_u16_le(mcp_bstream_t* s, uint16_t* v);
int mcp_bstream_read_u32_be(mcp_bstream_t* s, uint32_t* v);
int mcp_bstream_read_u32_le(mcp_bstream_t* s, uint32_t* v);
int mcp_bstream_read_u64_be(mcp_bstream_t* s, uint64_t* v);
int mcp_bstream_read_u64_le(mcp_bstream_t* s, uint64_t* v);
int mcp_bstream_read_f32_be(mcp_bstream_t* s, float* v);
int mcp_bstream_read_f32_le(mcp_bstream_t* s, float* v);
int mcp_bstream_read_f64_be(mcp_bstream_t* s, double* v);
int mcp_bstream_read_f64_le(mcp_bstream_t* s, double* v);
// Caller must free() the returned string.
int mcp_bstream_read_var_u32(mcp_bstream_t* s, uint32_t* v);
int mcp_bstream_read_var_u64(mcp_bstream_t* s, uint64_t* v);

// Peek without advancing the cursor.

