// SPDX-License-Identifier: LGPL-3.0-or-later
// AtmkChunkEncoder.h — AtmkMca chunk NBT encoder.
//
// Encodes a complete mcp_chunk_t* to NBT little-endian bytes
// (MCPE 0.15.x format, 128-block-tall chunks) including:
//   - Terrain (Blocks, Data, SkyLight, BlockLight, HeightMap, BiomeColors)
//   - Tile entities (List<CompoundTag>)
//   - Entities (List<CompoundTag>)
//   - Generation flags (Terrain, TerrainPopulated, LightPopulated)
//
// Output format mirrors the spec in ATMKA_FORMAT.md.
//
// IMPORTANT: this header MUST stay C-compatible (no <vector>/<memory>)
// because the implementation file is .c (compiled as C by CMake).
#pragma once

#include "level/format/Chunk.h"
#include "nbt/tag/Tag.h"

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Encode an mcp_chunk_t to NBT bytes (little-endian, AtmkMca format).
// Returns malloc'd buffer; caller must free(). *out_len receives size.
// Returns NULL on error.
//
// The NBT tree built mirrors PMMP's mcacpp LevelDB::saveChunk output
// with two extra fields (Tiles + Entities lists) for full persistence.
uint8_t* atmk_chunk_encode(const mcp_chunk_t* chunk, size_t* out_len);

#ifdef __cplusplus
}
#endif
