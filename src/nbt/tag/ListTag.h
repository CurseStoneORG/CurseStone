/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

 * CurseStone | Open Source Server Software
 *
 * Copyright (C) 2026 CurseStoneTeam
 *
 * Developed by CurseStoneTeam.
 * Built for an open and extensible Minecraft server ecosystem.
 *
 * Licensed under the GNU Lesser General Public License v3.0 or later.
 * You may study, modify, and redistribute this software under its license.
 *
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 * Repository: https://github.com/CurseStoneTeam/CurseStone
 *
 */
// SPDX-License-Identifier: LGPL-3.0-or-later
// ListTag — ordered list of tags all sharing the same element type.
// The element type is stored in `element_type`; all children MUST have
// that type (the NBT spec enforces homogeneity). Names of children
// inside a ListTag are NOT serialized.
#pragma once

#include "Tag.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mcp_list_tag {
    mcp_tag_t   header;
    mcp_tag_type_t element_type;
    mcp_tag_t** children;
    size_t      count;
    size_t      capacity;
} mcp_list_tag_t;

mcp_tag_t* mcp_list_tag_new(mcp_tag_type_t element_type);
mcp_tag_t* mcp_list_tag_new_named(const char* name, mcp_tag_type_t element_type);

// Append a child. The tag takes ownership of `child`. Type must match
// `element_type` (or both be EndTag for an empty list). Returns 0 on success.
int mcp_list_tag_append(mcp_tag_t* self, mcp_tag_t* child);

size_t mcp_list_tag_size(const mcp_tag_t* self);
const mcp_tag_t* mcp_list_tag_get(const mcp_tag_t* self, size_t index);

#ifdef __cplusplus
} // extern "C"
#endif
