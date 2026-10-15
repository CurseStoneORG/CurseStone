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
// StringTag — NBT string (UTF-8, length-prefixed).
#pragma once
#include "Tag.h"
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct mcp_string_tag {
    mcp_tag_t header;
    char* value; // owned UTF-8 buffer
    size_t length;
} mcp_string_tag_t;
mcp_tag_t* mcp_string_tag_new(const char* value, size_t len);
mcp_tag_t* mcp_string_tag_new_cstr(const char* value);
mcp_tag_t* mcp_string_tag_new_named_cstr(const char* name, const char* value);
const char* mcp_string_tag_get(const mcp_tag_t* tag);
#ifdef __cplusplus
}
#endif
