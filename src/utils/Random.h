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
 * @brief Provides a Mersenne Twister RNG.
 *
 * Used by world generation (noise seed) and gameplay randomness.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>


#define MCP_MT_N 624
#define MCP_MT_M 397

typedef struct mcp_random {
    uint32_t state[MCP_MT_N];
    size_t index;
} mcp_random_t;

// Initialize with a seed.
void mcp_random_seed(mcp_random_t* r, uint32_t seed);

// Auto-seed from time.

// Generate a random 32-bit unsigned int.
uint32_t mcp_random_next_u32(mcp_random_t* r);

// Generate a random int in [0, range).

// Random float in [0.0, 1.0).

// Random double in [0.0, 1.0).

