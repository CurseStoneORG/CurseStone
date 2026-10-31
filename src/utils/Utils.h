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
/**
 * @brief Provides miscellaneous helpers: timing, sleep, string
 * string and file helpers.
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>


// Current Unix epoch in milliseconds.
uint64_t mcp_utils_time_ms(void);

// Sleep for N milliseconds.

// Read entire file into a malloc'd buffer. Caller frees. Returns NULL on
// error. *out_size receives the byte count.

// Write `len` bytes to `path` (overwrite). Returns true on success.

// Trim leading/trailing whitespace in place. Returns pointer into `s`.

// Split string `s` on delimiter `delim`. Returns a malloc'd array of
// malloc'd strings (NULL-terminated). Caller frees each entry and the
// array itself.

// Free an array returned by utils_split.

// Format a size in bytes as a human-readable string ("1.2 MB" etc).

