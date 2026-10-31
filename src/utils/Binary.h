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
// Byte-order utilities.
// MCPE protocol 84 uses little-endian for most fields; NBT uses big-endian
// (Java-style) or little-endian (MCPE-style) depending on context.
#pragma once

#include <stdint.h>
#include <string.h>
#include <stdbool.h>


// ---- Read big-endian primitives ----
static inline uint8_t mcp_binary_read_u8(const void* p) {
    const uint8_t* b = (const uint8_t*)p;
    return b[0];
}
static inline int8_t mcp_binary_read_i8(const void* p) {
    return (int8_t)mcp_binary_read_u8(p);
}
static inline uint16_t mcp_binary_read_u16_be(const void* p) {
    const uint8_t* b = (const uint8_t*)p;
    return (uint16_t)((b[0] << 8) | b[1]);
}
static inline int16_t mcp_binary_read_i16_be(const void* p) {
    return (int16_t)mcp_binary_read_u16_be(p);
}
static inline uint32_t mcp_binary_read_u32_be(const void* p) {
    const uint8_t* b = (const uint8_t*)p;
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | (uint32_t)b[3];
}
static inline int32_t mcp_binary_read_i32_be(const void* p) {
    return (int32_t)mcp_binary_read_u32_be(p);
}
static inline uint64_t mcp_binary_read_u64_be(const void* p) {
    const uint8_t* b = (const uint8_t*)p;
    return ((uint64_t)b[0] << 56) | ((uint64_t)b[1] << 48) | ((uint64_t)b[2] << 40) |
           ((uint64_t)b[3] << 32) | ((uint64_t)b[4] << 24) | ((uint64_t)b[5] << 16) |
           ((uint64_t)b[6] << 8) | (uint64_t)b[7];
}
static inline int64_t mcp_binary_read_i64_be(const void* p) {
    return (int64_t)mcp_binary_read_u64_be(p);
}
static inline float mcp_binary_read_f32_be(const void* p) {
    uint32_t v = mcp_binary_read_u32_be(p);
    float f;
    memcpy(&f, &v, sizeof(f));
    return f;
}
static inline double mcp_binary_read_f64_be(const void* p) {
    uint64_t v = mcp_binary_read_u64_be(p);
    double d;
    memcpy(&d, &v, sizeof(d));
    return d;
}

// ---- Read little-endian primitives ----
static inline uint16_t mcp_binary_read_u16_le(const void* p) {
    const uint8_t* b = (const uint8_t*)p;
    return (uint16_t)((b[1] << 8) | b[0]);
}
static inline int16_t mcp_binary_read_i16_le(const void* p) {
    return (int16_t)mcp_binary_read_u16_le(p);
}
static inline uint32_t mcp_binary_read_u32_le(const void* p) {
    const uint8_t* b = (const uint8_t*)p;
    return ((uint32_t)b[3] << 24) | ((uint32_t)b[2] << 16) | ((uint32_t)b[1] << 8) | (uint32_t)b[0];
}
static inline int32_t mcp_binary_read_i32_le(const void* p) {
    return (int32_t)mcp_binary_read_u32_le(p);
}
static inline uint64_t mcp_binary_read_u64_le(const void* p) {
    const uint8_t* b = (const uint8_t*)p;
    return ((uint64_t)b[7] << 56) | ((uint64_t)b[6] << 48) | ((uint64_t)b[5] << 40) |
           ((uint64_t)b[4] << 32) | ((uint64_t)b[3] << 24) | ((uint64_t)b[2] << 16) |
           ((uint64_t)b[1] << 8) | (uint64_t)b[0];
}
static inline int64_t mcp_binary_read_i64_le(const void* p) {
    return (int64_t)mcp_binary_read_u64_le(p);
}
static inline float mcp_binary_read_f32_le(const void* p) {
    uint32_t v = mcp_binary_read_u32_le(p);
    float f;
    memcpy(&f, &v, sizeof(f));
    return f;
}
static inline double mcp_binary_read_f64_le(const void* p) {
    uint64_t v = mcp_binary_read_u64_le(p);
    double d;
    memcpy(&d, &v, sizeof(d));
    return d;
}

// ---- Write big-endian primitives ----
static inline void mcp_binary_write_u8(void* p, uint8_t v) {
    uint8_t* b = (uint8_t*)p;
    b[0] = v;
}
static inline void mcp_binary_write_u16_be(void* p, uint16_t v) {
    uint8_t* b = (uint8_t*)p;
    b[0] = (uint8_t)(v >> 8);
    b[1] = (uint8_t)(v);
}
static inline void mcp_binary_write_u32_be(void* p, uint32_t v) {
    uint8_t* b = (uint8_t*)p;
    b[0] = (uint8_t)(v >> 24);
    b[1] = (uint8_t)(v >> 16);
    b[2] = (uint8_t)(v >> 8);
    b[3] = (uint8_t)(v);
}
static inline void mcp_binary_write_u64_be(void* p, uint64_t v) {
    uint8_t* b = (uint8_t*)p;
    b[0] = (uint8_t)(v >> 56);
    b[1] = (uint8_t)(v >> 48);
    b[2] = (uint8_t)(v >> 40);
    b[3] = (uint8_t)(v >> 32);
    b[4] = (uint8_t)(v >> 24);
    b[5] = (uint8_t)(v >> 16);
    b[6] = (uint8_t)(v >> 8);
    b[7] = (uint8_t)(v);
}
static inline void mcp_binary_write_f32_be(void* p, float v) {
    uint32_t u;
    memcpy(&u, &v, sizeof(u));
    mcp_binary_write_u32_be(p, u);
}
static inline void mcp_binary_write_f64_be(void* p, double v) {
    uint64_t u;
    memcpy(&u, &v, sizeof(u));
    mcp_binary_write_u64_be(p, u);
}

// ---- Write little-endian primitives ----
static inline void mcp_binary_write_u16_le(void* p, uint16_t v) {
    uint8_t* b = (uint8_t*)p;
    b[0] = (uint8_t)(v);
    b[1] = (uint8_t)(v >> 8);
}
static inline void mcp_binary_write_u32_le(void* p, uint32_t v) {
    uint8_t* b = (uint8_t*)p;
    b[0] = (uint8_t)(v);
    b[1] = (uint8_t)(v >> 8);
    b[2] = (uint8_t)(v >> 16);
    b[3] = (uint8_t)(v >> 24);
}
static inline void mcp_binary_write_u64_le(void* p, uint64_t v) {
    uint8_t* b = (uint8_t*)p;
    b[0] = (uint8_t)(v);
    b[1] = (uint8_t)(v >> 8);
    b[2] = (uint8_t)(v >> 16);
    b[3] = (uint8_t)(v >> 24);
    b[4] = (uint8_t)(v >> 32);
    b[5] = (uint8_t)(v >> 40);
    b[6] = (uint8_t)(v >> 48);
    b[7] = (uint8_t)(v >> 56);
}
static inline void mcp_binary_write_f32_le(void* p, float v) {
    uint32_t u;
    memcpy(&u, &v, sizeof(u));
    mcp_binary_write_u32_le(p, u);
}
static inline void mcp_binary_write_f64_le(void* p, double v) {
    uint64_t u;
    memcpy(&u, &v, sizeof(u));
    mcp_binary_write_u64_le(p, u);
}

// ---- Unsigned VarInt / VarLong (LEB128-style) ----
// MCPE protocol uses these for sub-packet lengths and entity IDs.
static inline size_t mcp_varint_write_u32(uint8_t* out, uint32_t v) {
    size_t i = 0;
    while ((v & 0x80) != 0) {
        out[i++] = (uint8_t)((v & 0x7F) | 0x80);
        v >>= 7;
    }
    out[i++] = (uint8_t)v;
    return i;
}
static inline size_t mcp_varint_write_u64(uint8_t* out, uint64_t v) {
    size_t i = 0;
    while ((v & 0x80) != 0) {
        out[i++] = (uint8_t)((v & 0x7F) | 0x80);
        v >>= 7;
    }
    out[i++] = (uint8_t)v;
    return i;
}
// Reads a VarInt. Returns bytes consumed in *out_len; -1 on overflow/truncation.
static inline int mcp_varint_read_u32(const uint8_t* p, size_t max, uint32_t* out) {
    uint32_t result = 0;
    size_t shift = 0;
    for (size_t i = 0; i < max && i < 5; ++i) {
        uint8_t b = p[i];
        result |= (uint32_t)(b & 0x7F) << shift;
        if ((b & 0x80) == 0) {
            *out = result;
            return (int)(i + 1);
        }
        shift += 7;
    }
    return -1;
}
static inline int mcp_varint_read_u64(const uint8_t* p, size_t max, uint64_t* out) {
    uint64_t result = 0;
    size_t shift = 0;
    for (size_t i = 0; i < max && i < 10; ++i) {
        uint8_t b = p[i];
        result |= (uint64_t)(b & 0x7F) << shift;
        if ((b & 0x80) == 0) {
            *out = result;
            return (int)(i + 1);
        }
        shift += 7;
    }
    return -1;
}

// Signed VarInt / VarLong via zigzag encoding.
static inline uint32_t mcp_varint_zigzag_i32(int32_t v) {
    return (uint32_t)((v << 1) ^ (v >> 31));
}
static inline int32_t mcp_varint_unzigzag_i32(uint32_t v) {
    return (int32_t)((v >> 1) ^ -(int32_t)(v & 1));
}
static inline uint64_t mcp_varint_zigzag_i64(int64_t v) {
    return (uint64_t)((v << 1) ^ (v >> 63));
}
static inline int64_t mcp_varint_unzigzag_i64(uint64_t v) {
    return (int64_t)((v >> 1) ^ -(int64_t)(v & 1));
}

