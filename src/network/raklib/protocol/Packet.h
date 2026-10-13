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
// Packet — base RakNet packet interface. Pure C vtable.
#pragma once

#include "../../../utils/BinaryStream.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mcp_packet_vtable {
    uint8_t  (*id)(void);
    void     (*encode)(void* self, mcp_bstream_t* out);
    void     (*decode)(void* self, mcp_bstream_t* in);
    size_t   (*size_estimate)(const void* self);
    void     (*destroy)(void* self);
} mcp_packet_vtable_t;

typedef struct mcp_packet {
    const mcp_packet_vtable_t* vtable;
} mcp_packet_t;

#ifdef __cplusplus
} // extern "C"
#endif
