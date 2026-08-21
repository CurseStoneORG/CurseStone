// SPDX-License-Identifier: LGPL-3.0-or-later
// LongTag — signed 64-bit.
#pragma once
#include "Tag.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct mcp_long_tag {
    mcp_tag_t header;
    int64_t value;
} mcp_long_tag_t;
mcp_tag_t* mcp_long_tag_new(int64_t v);
mcp_tag_t* mcp_long_tag_new_named(const char* name, int64_t v);
#ifdef __cplusplus
}
#endif
