// SPDX-License-Identifier: LGPL-3.0-or-later
// NBT — top-level reader/writer. Pure C.
//
// `NBT` is the public façade: parse raw bytes into a Tag tree, or
// serialize a Tag tree back into bytes. Endianness selection lets
// the same code read both Java-format chunks (big-endian) and
// MCPE-format chunks (little-endian).
#pragma once

#include "tag/Tag.h"
#include "../utils/BinaryStream.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mcp_nbt {
    bool little_endian;
} mcp_nbt_t;

// Read a full named root tag from a BinaryStream. Returns NULL on EOF
// or parse error. Caller must mcp_tag_destroy() the result.
mcp_tag_t* mcp_nbt_read_root(mcp_bstream_t* s, bool little_endian);

// Read just a payload of the given type (no leading type byte, no name).
// Used internally for list elements and compound children.
mcp_tag_t* mcp_nbt_read_payload(mcp_bstream_t* s, mcp_tag_type_t type,
                                  bool little_endian);

// Read the type+name header of a compound child. Sets `*out_name` (caller
// frees) and `*out_type`. Returns 0 on success, -1 on EOF, +1 on EndTag.
int mcp_nbt_read_named_child_header(mcp_bstream_t* s, bool little_endian,
                                      mcp_tag_type_t* out_type,
                                      char** out_name);

// Serialize a full root tag into a BinaryStream.
void mcp_nbt_write_root(mcp_bstream_t* s, const mcp_tag_t* root,
                         bool little_endian);

// Convenience: read NBT from a raw byte buffer.
mcp_tag_t* mcp_nbt_read_bytes(const uint8_t* data, size_t len,
                                bool little_endian);

// Convenience: serialize NBT to a malloc'd byte buffer.
// Caller frees. *out_len receives the size.
uint8_t* mcp_nbt_write_bytes(const mcp_tag_t* root, bool little_endian,
                              size_t* out_len);

#ifdef __cplusplus
} // extern "C"
#endif
