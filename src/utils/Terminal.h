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
// Terminal — ANSI color code constants and tty detection. Pure C.
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ANSI escape codes (foreground)
#define MCP_TERM_BLACK     "\x1b[30m"
#define MCP_TERM_RED       "\x1b[31m"
#define MCP_TERM_GREEN     "\x1b[32m"
#define MCP_TERM_YELLOW    "\x1b[33m"
#define MCP_TERM_BLUE      "\x1b[34m"
#define MCP_TERM_MAGENTA   "\x1b[35m"
#define MCP_TERM_CYAN      "\x1b[36m"
#define MCP_TERM_WHITE     "\x1b[37m"

// Bright variants
#define MCP_TERM_BBLACK    "\x1b[90m"
#define MCP_TERM_BRED      "\x1b[91m"
#define MCP_TERM_BGREEN    "\x1b[92m"
#define MCP_TERM_BYELLOW   "\x1b[93m"
#define MCP_TERM_BBLUE     "\x1b[94m"
#define MCP_TERM_BMAGENTA  "\x1b[95m"
#define MCP_TERM_BCYAN     "\x1b[96m"
#define MCP_TERM_BWHITE    "\x1b[97m"

// Styles
#define MCP_TERM_BOLD      "\x1b[1m"
#define MCP_TERM_ITALIC    "\x1b[3m"
#define MCP_TERM_UNDERLINE "\x1b[4m"
#define MCP_TERM_STRIKE    "\x1b[9m"
#define MCP_TERM_RESET     "\x1b[0m"

// Returns true if stdout is a tty and ANSI codes should be emitted.
bool mcp_terminal_has_color(void);

// Initialize terminal detection. Call once at startup; subsequent
// calls to mcp_terminal_has_color() read a cached result.
void mcp_terminal_init(bool force_color);

#ifdef __cplusplus
} // extern "C"
#endif
