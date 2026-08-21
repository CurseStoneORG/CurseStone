// SPDX-License-Identifier: LGPL-3.0-or-later
// ShortTag — signed 16-bit.
#pragma once

#include "Tag.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mcp_short_tag {
    mcp_tag_t header;
    int16_t value;
} mcp_short_tag_t;

mcp_tag_t* mcp_short_tag_new(int16_t v);
mcp_tag_t* mcp_short_tag_new_named(const char* name, int16_t v);

#ifdef __cplusplus
} // extern "C"
#endif
