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
// BinaryStream implementation. Pure C.
#include "BinaryStream.h"
#include "Binary.h"

#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#define MCP_BS_INITIAL_CAP 64

static bool ensure_capacity(mcp_bstream_t* s, size_t need) {
    if (s->capacity >= need) return true;
    size_t newCap = s->capacity == 0 ? MCP_BS_INITIAL_CAP : s->capacity;
    while (newCap < need) newCap *= 2;
    uint8_t* p = (uint8_t*)realloc(s->data, newCap);
    if (!p) return false;
    s->data     = p;
    s->capacity = newCap;
    return true;
}

void mcp_bstream_init(mcp_bstream_t* s) {
    s->data = NULL;
    s->size = s->capacity = s->offset = 0;
}

void mcp_bstream_init_read(mcp_bstream_t* s, const uint8_t* data, size_t len) {
    // Make a writable copy so callers can free their buffer immediately.
    s->data = (uint8_t*)malloc(len ? len : 1);
    if (s->data && data && len) {
        memcpy(s->data, data, len);
    }
    s->size = s->capacity = len;
    s->offset = 0;
}

void mcp_bstream_free(mcp_bstream_t* s) {
    if (s->data) {
        free(s->data);
        s->data = NULL;
    }
    s->size = s->capacity = s->offset = 0;
}

void mcp_bstream_reset(mcp_bstream_t* s) {
    s->size = s->offset = 0;
}

size_t mcp_bstream_remaining(const mcp_bstream_t* s) {
    return s->size > s->offset ? s->size - s->offset : 0;
}

void mcp_bstream_write_bytes(mcp_bstream_t* s, const void* data, size_t len) {
    if (!ensure_capacity(s, s->size + len)) return;
    if (len) memcpy(s->data + s->size, data, len);
    s->size += len;
}

void mcp_bstream_write_u8(mcp_bstream_t* s, uint8_t v) {
    mcp_bstream_write_bytes(s, &v, 1);
}
void mcp_bstream_write_i8(mcp_bstream_t* s, int8_t v) {
    mcp_bstream_write_bytes(s, &v, 1);
}
void mcp_bstream_write_u16_be(mcp_bstream_t* s, uint16_t v) {
    uint8_t buf[2];
    mcp_binary_write_u16_be(buf, v);
    mcp_bstream_write_bytes(s, buf, 2);
}
void mcp_bstream_write_u16_le(mcp_bstream_t* s, uint16_t v) {
    uint8_t buf[2];
    mcp_binary_write_u16_le(buf, v);
    mcp_bstream_write_bytes(s, buf, 2);
}
void mcp_bstream_write_u32_be(mcp_bstream_t* s, uint32_t v) {
    uint8_t buf[4];
    mcp_binary_write_u32_be(buf, v);
    mcp_bstream_write_bytes(s, buf, 4);
}
void mcp_bstream_write_u32_le(mcp_bstream_t* s, uint32_t v) {
    uint8_t buf[4];
    mcp_binary_write_u32_le(buf, v);
    mcp_bstream_write_bytes(s, buf, 4);
}
void mcp_bstream_write_u64_be(mcp_bstream_t* s, uint64_t v) {
    uint8_t buf[8];
    mcp_binary_write_u64_be(buf, v);
    mcp_bstream_write_bytes(s, buf, 8);
}
void mcp_bstream_write_u64_le(mcp_bstream_t* s, uint64_t v) {
    uint8_t buf[8];
    mcp_binary_write_u64_le(buf, v);
    mcp_bstream_write_bytes(s, buf, 8);
}
void mcp_bstream_write_f32_be(mcp_bstream_t* s, float v) {
    uint8_t buf[4];
    mcp_binary_write_f32_be(buf, v);
    mcp_bstream_write_bytes(s, buf, 4);
}
void mcp_bstream_write_f32_le(mcp_bstream_t* s, float v) {
    uint8_t buf[4];
    mcp_binary_write_f32_le(buf, v);
    mcp_bstream_write_bytes(s, buf, 4);
}
void mcp_bstream_write_f64_be(mcp_bstream_t* s, double v) {
    uint8_t buf[8];
    mcp_binary_write_f64_be(buf, v);
    mcp_bstream_write_bytes(s, buf, 8);
}
void mcp_bstream_write_f64_le(mcp_bstream_t* s, double v) {
    uint8_t buf[8];
    mcp_binary_write_f64_le(buf, v);
    mcp_bstream_write_bytes(s, buf, 8);
}

void mcp_bstream_write_string(mcp_bstream_t* s, const char* str) {
    size_t len = str ? strlen(str) : 0;
    uint8_t lenbuf[5];
    size_t lenlen = mcp_varint_write_u32(lenbuf, (uint32_t)len);
    mcp_bstream_write_bytes(s, lenbuf, lenlen);
    if (len) mcp_bstream_write_bytes(s, str, len);
}

void mcp_bstream_write_var_u32(mcp_bstream_t* s, uint32_t v) {
    uint8_t buf[5];
    size_t  n = mcp_varint_write_u32(buf, v);
    mcp_bstream_write_bytes(s, buf, n);
}
void mcp_bstream_write_var_u64(mcp_bstream_t* s, uint64_t v) {
    uint8_t buf[10];
    size_t  n = mcp_varint_write_u64(buf, v);
    mcp_bstream_write_bytes(s, buf, n);
}
void mcp_bstream_write_var_i32(mcp_bstream_t* s, int32_t v) {
    mcp_bstream_write_var_u32(s, mcp_varint_zigzag_i32(v));
}
void mcp_bstream_write_var_i64(mcp_bstream_t* s, int64_t v) {
    mcp_bstream_write_var_u64(s, mcp_varint_zigzag_i64(v));
}

int mcp_bstream_read_bytes(mcp_bstream_t* s, void* out, size_t len) {
    if (mcp_bstream_remaining(s) < len) return -1;
    if (len) memcpy(out, s->data + s->offset, len);
    s->offset += len;
    return 0;
}
int mcp_bstream_read_u8(mcp_bstream_t* s, uint8_t* v) {
    if (mcp_bstream_remaining(s) < 1) return -1;
    *v = s->data[s->offset++];
    return 0;
}
int mcp_bstream_read_i8(mcp_bstream_t* s, int8_t* v) {
    uint8_t u;
    if (mcp_bstream_read_u8(s, &u) != 0) return -1;
    *v = (int8_t)u;
    return 0;
}
int mcp_bstream_read_u16_be(mcp_bstream_t* s, uint16_t* v) {
    uint8_t b[2];
    if (mcp_bstream_read_bytes(s, b, 2) != 0) return -1;
    *v = mcp_binary_read_u16_be(b);
    return 0;
}
int mcp_bstream_read_u16_le(mcp_bstream_t* s, uint16_t* v) {
    uint8_t b[2];
    if (mcp_bstream_read_bytes(s, b, 2) != 0) return -1;
    *v = mcp_binary_read_u16_le(b);
    return 0;
}
int mcp_bstream_read_u32_be(mcp_bstream_t* s, uint32_t* v) {
    uint8_t b[4];
    if (mcp_bstream_read_bytes(s, b, 4) != 0) return -1;
    *v = mcp_binary_read_u32_be(b);
    return 0;
}
int mcp_bstream_read_u32_le(mcp_bstream_t* s, uint32_t* v) {
    uint8_t b[4];
    if (mcp_bstream_read_bytes(s, b, 4) != 0) return -1;
    *v = mcp_binary_read_u32_le(b);
    return 0;
}
int mcp_bstream_read_u64_be(mcp_bstream_t* s, uint64_t* v) {
    uint8_t b[8];
    if (mcp_bstream_read_bytes(s, b, 8) != 0) return -1;
    *v = mcp_binary_read_u64_be(b);
    return 0;
}
int mcp_bstream_read_u64_le(mcp_bstream_t* s, uint64_t* v) {
    uint8_t b[8];
    if (mcp_bstream_read_bytes(s, b, 8) != 0) return -1;
    *v = mcp_binary_read_u64_le(b);
    return 0;
}
int mcp_bstream_read_f32_be(mcp_bstream_t* s, float* v) {
    uint8_t b[4];
    if (mcp_bstream_read_bytes(s, b, 4) != 0) return -1;
    *v = mcp_binary_read_f32_be(b);
    return 0;
}
int mcp_bstream_read_f32_le(mcp_bstream_t* s, float* v) {
    uint8_t b[4];
    if (mcp_bstream_read_bytes(s, b, 4) != 0) return -1;
    *v = mcp_binary_read_f32_le(b);
    return 0;
}
int mcp_bstream_read_f64_be(mcp_bstream_t* s, double* v) {
    uint8_t b[8];
    if (mcp_bstream_read_bytes(s, b, 8) != 0) return -1;
    *v = mcp_binary_read_f64_be(b);
    return 0;
}
int mcp_bstream_read_f64_le(mcp_bstream_t* s, double* v) {
    uint8_t b[8];
    if (mcp_bstream_read_bytes(s, b, 8) != 0) return -1;
    *v = mcp_binary_read_f64_le(b);
    return 0;
}

char* mcp_bstream_read_string(mcp_bstream_t* s) {
    uint32_t len;
    if (mcp_bstream_read_var_u32(s, &len) != 0) return NULL;
    if (mcp_bstream_remaining(s) < len) return NULL;
    char* str = (char*)malloc((size_t)len + 1);
    if (!str) return NULL;
    if (len) memcpy(str, s->data + s->offset, len);
    str[len] = '\0';
    s->offset += len;
    return str;
}

int mcp_bstream_read_var_u32(mcp_bstream_t* s, uint32_t* v) {
    int n = mcp_varint_read_u32(s->data + s->offset,
                                mcp_bstream_remaining(s), v);
    if (n < 0) return -1;
    s->offset += (size_t)n;
    return 0;
}
int mcp_bstream_read_var_u64(mcp_bstream_t* s, uint64_t* v) {
    int n = mcp_varint_read_u64(s->data + s->offset,
                                mcp_bstream_remaining(s), v);
    if (n < 0) return -1;
    s->offset += (size_t)n;
    return 0;
}
int mcp_bstream_read_var_i32(mcp_bstream_t* s, int32_t* v) {
    uint32_t u;
    if (mcp_bstream_read_var_u32(s, &u) != 0) return -1;
    *v = mcp_varint_unzigzag_i32(u);
    return 0;
}
int mcp_bstream_read_var_i64(mcp_bstream_t* s, int64_t* v) {
    uint64_t u;
    if (mcp_bstream_read_var_u64(s, &u) != 0) return -1;
    *v = mcp_varint_unzigzag_i64(u);
    return 0;
}

int mcp_bstream_peek_u8(const mcp_bstream_t* s, uint8_t* v) {
    if (mcp_bstream_remaining(s) < 1) return -1;
    *v = s->data[s->offset];
    return 0;
}
