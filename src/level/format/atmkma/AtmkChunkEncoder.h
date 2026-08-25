// SPDX-License-Identifier: LGPL-3.0-or-later
// AtmkChunkEncoder.h — AtmkMca chunk NBT encoder (C-compatible).
#pragma once
#include "level/format/Chunk.h"
#include "nbt/tag/Tag.h"
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
uint8_t* atmk_chunk_encode(const mcp_chunk_t* chunk, size_t* out_len);
#ifdef __cplusplus
}
#endif
