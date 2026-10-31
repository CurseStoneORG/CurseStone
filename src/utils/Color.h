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
/** @brief Stores an RGBA8 color struct and provides helpers. Pure C. */
#pragma once

#include <stdint.h>


typedef struct mcp_color {
    uint8_t r, g, b, a;
} mcp_color_t;

#define MCP_COLOR_BLACK ((mcp_color_t){0, 0, 0, 255})
#define MCP_COLOR_WHITE ((mcp_color_t){255, 255, 255, 255})
#define MCP_COLOR_RED ((mcp_color_t){255, 0, 0, 255})
#define MCP_COLOR_GREEN ((mcp_color_t){0, 255, 0, 255})
#define MCP_COLOR_BLUE ((mcp_color_t){0, 0, 255, 255})
#define MCP_COLOR_YELLOW ((mcp_color_t){255, 255, 0, 255})
#define MCP_COLOR_CYAN ((mcp_color_t){0, 255, 255, 255})
#define MCP_COLOR_MAGENTA ((mcp_color_t){255, 0, 255, 255})
#define MCP_COLOR_DIRT_BROWN ((mcp_color_t){134, 96, 67, 255})
#define MCP_COLOR_GRASS_GREEN ((mcp_color_t){120, 180, 80, 255})
#define MCP_COLOR_STONE_GRAY ((mcp_color_t){128, 128, 128, 255})

// Pack to/from 0xAARRGGBB uint32.

// Linear interpolation; t=0 → a, t=1 → b.

