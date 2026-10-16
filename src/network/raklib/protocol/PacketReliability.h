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
// PacketReliability — RakNet encapsulated packet reliability levels.
// Values match raklib/protocol/PacketReliability (the reference server)
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Reliability constants (3 bits in encapsulated header flags >> 5)
#define MCP_RELIABILITY_UNRELIABLE 0
#define MCP_RELIABILITY_UNRELIABLE_SEQUENCED 1
#define MCP_RELIABILITY_RELIABLE 2
#define MCP_RELIABILITY_RELIABLE_ORDERED 3
#define MCP_RELIABILITY_RELIABLE_SEQUENCED 4
#define MCP_RELIABILITY_RELIABLE_WITH_ACK_RECEIPT 6
#define MCP_RELIABILITY_RELIABLE_ORDERED_WITH_ACK_RECEIPT 7

// Keep the old typedef for compatibility
typedef uint8_t mcp_reliability_t;

#ifdef __cplusplus
} // extern "C"
#endif
