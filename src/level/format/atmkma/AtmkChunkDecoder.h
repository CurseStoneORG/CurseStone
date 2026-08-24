// SPDX-License-Identifier: LGPL-3.0-or-later
// AtmkChunkDecoder.h — AtmkMca chunk NBT decoder.
//
// Decodes NBT little-endian bytes (encoded by AtmkChunkEncoder) back
// into a complete mcp_chunk_t*, including:
//   - Terrain (Blocks, Data, SkyLight, BlockLight, HeightMap, BiomeColors)
//   - Tile entities (raw NBT blobs attached to chunk->tile_entities)
//   - Entities (raw NBT blobs attached to chunk->entities)
//   - Generation flags
//
// IMPORTANT: this header MUST stay C-compatible (no <vector>/<memory>).
#pragma once

#include "level/format/Chunk.h"
#include "nbt/tag/Tag.h"

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Decode NBT bytes (little-endian) into an mcp_chunk_t*.
// Returns a new mcp_chunk_t (caller must mcp_chunk_destroy) or NULL.
// The chunk's x/z are taken from the NBT.
mcp_chunk_t* atmk_chunk_decode(const uint8_t* data, size_t len);

#ifdef __cplusplus
}
#endif
