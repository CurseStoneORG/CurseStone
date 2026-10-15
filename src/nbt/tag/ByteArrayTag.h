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
    size_t length;
} mcp_byte_array_tag_t;
mcp_tag_t* mcp_byte_array_tag_new(const uint8_t* data, size_t len);
mcp_tag_t* mcp_byte_array_tag_new_named(const char* name, const uint8_t* data, size_t len);
#ifdef __cplusplus
}
#endif
