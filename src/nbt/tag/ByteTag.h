// SPDX-License-Identifier: LGPL-3.0-or-later
// ByteTag — NBT tag holding a single signed 8-bit integer.
#pragma once

#include "Tag.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mcp_byte_tag {
    mcp_tag_t header;   // must be first
    int8_t value;
} mcp_byte_tag_t;

mcp_tag_t* mcp_byte_tag_new(int8_t v);
mcp_tag_t* mcp_byte_tag_new_named(const char* name, int8_t v);

#ifdef __cplusplus
} // extern "C"
#endif
