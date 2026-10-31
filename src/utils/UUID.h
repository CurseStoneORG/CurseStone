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
// UUID — RFC 4122 v4 UUID generation + parsing.
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>


typedef struct mcp_uuid {
    uint8_t bytes[16];
} mcp_uuid_t;

// Generate a v4 (random) UUID. Returns true on success.
bool mcp_uuid_v4(mcp_uuid_t* out);

// Parse a 36-char "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx" UUID.

// Format a UUID as a 36-char string in `out` (must be >= 37 bytes).

// Compare two UUIDs. Returns -1 / 0 / 1 like memcmp.

