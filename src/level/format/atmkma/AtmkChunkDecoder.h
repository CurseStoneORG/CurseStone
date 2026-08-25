// SPDX-License-Identifier: LGPL-3.0-or-later
// AtmkChunkDecoder.h — AtmkMca chunk NBT decoder (C-compatible).
#pragma once
#include "level/format/Chunk.h"
#include "nbt/tag/Tag.h"
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
mcp_chunk_t* atmk_chunk_decode(const uint8_t* data, size_t len);
#ifdef __cplusplus
}
#endif
