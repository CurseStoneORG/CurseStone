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
// UUID.cpp — random v4 generation and string parsing.
#include "UUID.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int seeded = 0;
static void ensure_seed(void) {
    if (seeded)
        return;
    // Simple seed from time + clock; sufficient for non-cryptographic UUIDs.
    unsigned int seed = (unsigned int)time(NULL) ^ (unsigned int)((uintptr_t)&seeded);
    srand(seed);
    seeded = 1;
}

bool mcp_uuid_v4(mcp_uuid_t* out) {
    if (!out)
        return false;
    ensure_seed();
    for (int i = 0; i < 16; ++i) {
        out->bytes[i] = (uint8_t)(rand() & 0xFF);
    }
    // Set version (4) and variant (RFC 4122)
    out->bytes[6] = (uint8_t)((out->bytes[6] & 0x0F) | 0x40);
    out->bytes[8] = (uint8_t)((out->bytes[8] & 0x3F) | 0x80);
    return true;
}

static int hex_digit(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}



