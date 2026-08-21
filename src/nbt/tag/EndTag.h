// SPDX-License-Identifier: LGPL-3.0-or-later
// EndTag — sentinel marking the end of a CompoundTag or an empty ListTag.
#pragma once

#include "Tag.h"

#ifdef __cplusplus
extern "C" {
#endif

// Construct an EndTag (no allocation needed beyond the base struct).
mcp_tag_t* mcp_end_tag_new(void);

#ifdef __cplusplus
} // extern "C"
#endif
