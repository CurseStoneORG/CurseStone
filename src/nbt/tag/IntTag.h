// SPDX-License-Identifier: LGPL-3.0-or-later
// IntTag — signed 32-bit.
#pragma once
#include "Tag.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct mcp_int_tag {
    mcp_tag_t header;
    int32_t value;
} mcp_int_tag_t;
mcp_tag_t* mcp_int_tag_new(int32_t v);
mcp_tag_t* mcp_int_tag_new_named(const char* name, int32_t v);
#ifdef __cplusplus
}
#endif
