// SPDX-License-Identifier: LGPL-3.0-or-later
// AnvilChunkCodec.h — encodes/decodes mcp_chunk_t* to/from the Nukkit
// Anvil chunk NBT payload (big-endian, ZLIB-compressed separately).
//
// Ported from Nukkit InfinityEdition's
// `cn.nukkit.level.format.anvil.Chunk` (toBinary / fromBinary).
//
// On-disk chunk NBT layout (after ZLIB inflation):
//
//   Root CompoundTag (name "")
//     └── "Level" : CompoundTag
//           ├── "xPos"            : Int
//           ├── "zPos"            : Int
//           ├── "LastUpdate"      : Long
//           ├── "InhabitedTime"   : Long
//           ├── "vxl"             : Byte         (CurseStone-specific marker)
//           ├── "TerrainGenerated": Byte (0/1)
//           ├── "TerrainPopulated": Byte (0/1)
//           ├── "LightPopulated"  : Byte (0/1)
//           ├── "Sections"        : ListTag<CompoundTag>
//           │     each: { "Y":Byte, "Blocks":ByteArray(4096),
//           │              "Data":ByteArray(2048),
//           │              "BlockLight":ByteArray(2048),
//           │              "SkyLight":ByteArray(2048) }
//           ├── "BiomeColors"     : IntArray(256)   (BE ints, RGBA-ish)
//           ├── "HeightMap"       : IntArray(256)   (BE ints)
//           ├── "ExtraData"       : ByteArray       (var-int count + entries)
//           ├── "Entities"        : ListTag<CompoundTag>
//           └── "TileEntities"    : ListTag<CompoundTag>
//
// The CurseStone chunk has 16 sections (256-tall). We persist only the
// lower 8 sections (Y 0..7, i.e. block Y 0..127) since MCPE 0.15.x is
// 128-tall — matching what Nukkit stores in its Anvil format. Upper
// sections are dropped on save and zero-initialized on load.
#pragma once

#include "level/format/Chunk.h"
#include "nbt/tag/Tag.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Encode a chunk to NBT big-endian bytes (NOT compressed). Caller frees.
// Returns NULL on failure. *out_len receives the byte count.
uint8_t* anvil_chunk_encode(const mcp_chunk_t* chunk, size_t* out_len);

// Decode NBT big-endian bytes (already inflated — NOT compressed) into a
// fresh mcp_chunk_t*. Caller must mcp_chunk_destroy() the result.
// Returns NULL on parse error.
mcp_chunk_t* anvil_chunk_decode(const uint8_t* data, size_t len);

#ifdef __cplusplus
} // extern "C"
#endif
