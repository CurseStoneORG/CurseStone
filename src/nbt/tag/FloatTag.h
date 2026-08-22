// SPDX-License-Identifier: LGPL-3.0-or-later
// FloatTag — 32-bit IEEE 754.
#pragma once
#include "Tag.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct mcp_float_tag {
    mcp_tag_t header;
    float value;
} mcp_float_tag_t;
mcp_tag_t* mcp_float_tag_new(float v);
mcp_tag_t* mcp_float_tag_new_named(const char* name, float v);
#ifdef __cplusplus
}
#endif
