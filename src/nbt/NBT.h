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
// NBT — top-level reader/writer.
//
// `NBT` is the public façade: parse raw bytes into a Tag tree, or
// serialize a Tag tree back into bytes. Endianness selection lets
// the same code read both Java-format chunks (big-endian) and
// MCPE-format chunks (little-endian).
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "../utils/BinaryStream.h"
#include "tag/Tag.h"

#ifdef __cplusplus
extern "C" {
#endif

// Parse a full named root tag. Returns NULL on EOF or parse error.
// Caller owns the result and releases it with mcp_tag_destroy().
mcp_tag_t* mcp_nbt_read_bytes(const uint8_t* data, size_t len, bool little_endian);

// Serialize a root tag into a malloc'd buffer. Returns NULL on allocation
// failure. Caller frees the result (single C allocation on this boundary;
// C++ callers hold it in utils/CRaii.h BufferPtr).
uint8_t* mcp_nbt_write_bytes(const mcp_tag_t* root, bool little_endian, size_t* out_len);

#ifdef __cplusplus
} // extern "C"
#endif
