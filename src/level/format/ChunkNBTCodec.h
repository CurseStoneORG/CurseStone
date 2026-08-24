// SPDX-License-Identifier: LGPL-3.0-or-later
// ChunkNBTCodec.h — MCPE 0.15.x chunk NBT serialization.
//
// Format reference: PMMP McRegion format (mcacpp) for protocol 84.
// The chunk is encoded as a little-endian NBT compound with the
// following structure (PMMP LevelDB::loadChunk / saveChunk):
//
//   TAG_Compound("") {
//     TAG_Int    "xPos"
//     TAG_Int    "zPos"
//     TAG_Byte   "vxl"        (version = 0 for MCPE 0.15)
//     TAG_Byte_Array "blocks"      (32768 bytes = 16*16*128 block IDs)
//     TAG_Byte_Array "data"        (16384 bytes nibble-packed block data)
//     TAG_Byte_Array "skyLight"    (16384 bytes nibble-packed)
//     TAG_Byte_Array "blockLight"  (16384 bytes nibble-packed)
//     TAG_Byte_Array "heightMap"    (256 bytes, one byte per column)
//     TAG_Byte_Array "biomeColors" (1024 bytes = 256 cols × 4 bytes RGBA)
//     TAG_Int_Array  "extraData"    (usually empty)
//   }
//
// MCPE 0.15 chunks are 128 high (not 256). When loading a chunk with
// blocks at y >= 128 they are silently dropped (PMMP behaviour).
//
// All multi-byte values use little-endian (MCPE NBT convention),
// EXCEPT the inner ByteArray payloads which are raw byte arrays.
#pragma once

#include "level/format/Chunk.h"
#include "nbt/tag/Tag.h"

#include <vector>
#include <cstdint>
#include <memory>

#ifdef __cplusplus
extern "C" {
#endif

// Encode an mcp_chunk_t to NBT bytes (little-endian, PMMP mcacpp format).
// Returns malloc'd buffer; caller must free(). *out_len receives size.
// Returns nullptr on error.
uint8_t* mcp_chunk_nbt_encode(const mcp_chunk_t* chunk, size_t* out_len);

// Decode NBT bytes (little-endian) into an mcp_chunk_t.
// Returns a new mcp_chunk_t (caller must mcp_chunk_destroy) or nullptr.
// The chunk's x/z are taken from the NBT.
mcp_chunk_t* mcp_chunk_nbt_decode(const uint8_t* data, size_t len);

#ifdef __cplusplus
}
#endif
