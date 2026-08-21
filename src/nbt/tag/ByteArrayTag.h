// SPDX-License-Identifier: LGPL-3.0-or-later
// ByteArrayTag — NBT byte array (signed int32 length prefix + raw bytes).
#pragma once
#include "Tag.h"
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct mcp_byte_array_tag {
    mcp_tag_t header;
    uint8_t* data;
    size_t   length;
} mcp_byte_array_tag_t;
mcp_tag_t* mcp_byte_array_tag_new(const uint8_t* data, size_t len);
mcp_tag_t* mcp_byte_array_tag_new_named(const char* name, const uint8_t* data, size_t len);
#ifdef __cplusplus
}
#endif
