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
// Tag — base NBT tag interface (opaque handle + free functions).
//
// NBT (Named Binary Tag) is Minecraft's serialization format. Used for:
//   - Anvil chunk data, level.dat, and player .dat files
//   - Block entity (tile) data: signs, chests, furnaces
//   - Item NBT data (enchantments, custom names, durability)
//
// Endianness:
//   - Java/NBT big-endian (Anvil chunks, level.dat, player .dat)
//   - MCPE little-endian (network packets, item extra data)
//
// The tag tree is implemented in C++ (see tag/TagDefs.h) with RAII
// payloads; this header only exposes the type ids and the owning
// mcp_tag_t handle. Every tag returned by an mcp_*_tag_new function is
// owned by the caller and must be released with mcp_tag_destroy().
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum mcp_tag_type {
    MCP_TAG_END = 0,
    MCP_TAG_BYTE = 1,
    MCP_TAG_SHORT = 2,
    MCP_TAG_INT = 3,
    MCP_TAG_LONG = 4,
    MCP_TAG_FLOAT = 5,
    MCP_TAG_DOUBLE = 6,
    MCP_TAG_BYTE_ARRAY = 7,
    MCP_TAG_STRING = 8,
    MCP_TAG_LIST = 9,
    MCP_TAG_COMPOUND = 10,
    MCP_TAG_INT_ARRAY = 11,
} mcp_tag_type_t;

typedef struct mcp_tag mcp_tag_t;

// Free a tag tree (recursively destroys children).
void mcp_tag_destroy(mcp_tag_t* tag);

// Get the type of a tag (MCP_TAG_END for NULL).
mcp_tag_type_t mcp_tag_get_type(const mcp_tag_t* tag);

// Set the name. Copies the string (caller keeps ownership of `name`).
void mcp_tag_set_name(mcp_tag_t* tag, const char* name, size_t len);
void mcp_tag_set_name_cstr(mcp_tag_t* tag, const char* name);

#ifdef __cplusplus
} // extern "C"
#endif
