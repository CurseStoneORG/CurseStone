/*
 _____                     _____ _
/  __ \                   /  ___| |
| |  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
\____/\ |_| | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
 * CurseStone | Open Source Server Software
 *
 * Copyright (C) 2026 CurseStoneTeam
 *
 * Licensed under the GNU Lesser General Public License v3.0 or later.
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 *
 */
// SPDX-License-Identifier: LGPL-3.0-or-later
// Reliability — RakNet encapsulated packet reliability levels.
// Values match the RakNet wire format (3 bits in the encapsulated
// header flags, shifted >> 5). Kept as plain #defines so both the C
// raklib core (Session.c / SessionManager.c) and the C++ interface
// (RakLibInterface.cpp) can use them.
#pragma once
#include <stdint.h>


#define MCP_RELIABILITY_UNRELIABLE 0
#define MCP_RELIABILITY_UNRELIABLE_SEQUENCED 1
#define MCP_RELIABILITY_RELIABLE 2
#define MCP_RELIABILITY_RELIABLE_ORDERED 3
#define MCP_RELIABILITY_RELIABLE_SEQUENCED 4
#define MCP_RELIABILITY_RELIABLE_WITH_ACK_RECEIPT 6
#define MCP_RELIABILITY_RELIABLE_ORDERED_WITH_ACK_RECEIPT 7

typedef uint8_t mcp_reliability_t;

