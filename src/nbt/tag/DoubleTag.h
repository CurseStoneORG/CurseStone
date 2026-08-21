// SPDX-License-Identifier: LGPL-3.0-or-later
// DoubleTag — 64-bit IEEE 754.
#pragma once
#include "Tag.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct mcp_double_tag {
    mcp_tag_t header;
    double value;
} mcp_double_tag_t;
mcp_tag_t* mcp_double_tag_new(double v);
mcp_tag_t* mcp_double_tag_new_named(const char* name, double v);
#ifdef __cplusplus
}
#endif
