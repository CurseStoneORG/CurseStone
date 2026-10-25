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
// IntArrayTag — NBT TAG_Int_Array (uint32 count prefix + int32s).
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "Tag.h"

#ifdef __cplusplus
extern "C" {
#endif

mcp_tag_t* mcp_int_array_tag_new(const int32_t* data, size_t count);
mcp_tag_t* mcp_int_array_tag_new_named(const char* name, const int32_t* data, size_t count);

// Returns the payload ints and stores their count in *out_count. Returns
// NULL when the tag is missing or not an int array. A valid empty array
// yields a non-NULL pointer and *out_count == 0.
const int32_t* mcp_int_array_tag_data(const mcp_tag_t* tag, size_t* out_count);

#ifdef __cplusplus
} // extern "C"
#endif
