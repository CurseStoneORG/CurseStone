// SPDX-License-Identifier: LGPL-3.0-or-later
// BinaryStream — dynamic byte buffer with read/write cursor.
// Used by RakLib packets, NBT, and the MCPE protocol packets.
// Pure C; mirrors BinaryStream.h in old PocketMine/Aquamarine.
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mcp_bstream {
    uint8_t* data;
    size_t   size;       // bytes used
    size_t   capacity;  // bytes allocated
    size_t   offset;     // read cursor
} mcp_bstream_t;

// Lifecycle
void mcp_bstream_init(mcp_bstream_t* s);
void mcp_bstream_init_read(mcp_bstream_t* s, const uint8_t* data, size_t len);
void mcp_bstream_free(mcp_bstream_t* s);

// Reset (keeps allocation, sets size and offset to 0)
void mcp_bstream_reset(mcp_bstream_t* s);

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
void mcp_bstream_write_string(mcp_bstream_t* s, const char* str);
void mcp_bstream_write_var_u32(mcp_bstream_t* s, uint32_t v);
void mcp_bstream_write_var_u64(mcp_bstream_t* s, uint64_t v);
void mcp_bstream_write_var_i32(mcp_bstream_t* s, int32_t v);
void mcp_bstream_write_var_i64(mcp_bstream_t* s, int64_t v);

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
char* mcp_bstream_read_string(mcp_bstream_t* s);
int   mcp_bstream_read_var_u32(mcp_bstream_t* s, uint32_t* v);
int   mcp_bstream_read_var_u64(mcp_bstream_t* s, uint64_t* v);
int   mcp_bstream_read_var_i32(mcp_bstream_t* s, int32_t* v);
int   mcp_bstream_read_var_i64(mcp_bstream_t* s, int64_t* v);

// Peek without advancing the cursor.
int mcp_bstream_peek_u8(const mcp_bstream_t* s, uint8_t* v);

#ifdef __cplusplus
} // extern "C"
#endif
