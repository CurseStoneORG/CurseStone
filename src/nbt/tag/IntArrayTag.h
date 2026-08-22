// SPDX-License-Identifier: LGPL-3.0-or-later
// IntArrayTag — NBT int array (signed int32 length prefix + int32 values).
#pragma once
#include "Tag.h"
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct mcp_int_array_tag {
    mcp_tag_t header;
    int32_t* data;
    size_t   count;
} mcp_int_array_tag_t;
mcp_tag_t* mcp_int_array_tag_new(const int32_t* data, size_t count);
mcp_tag_t* mcp_int_array_tag_new_named(const char* name, const int32_t* data, size_t count);
#ifdef __cplusplus
}
#endif
