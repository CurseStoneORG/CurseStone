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
/** @brief Provides Minecraft §-color code conversion helpers. Pure C. */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Minecraft § color/format codes (Minecraft BE). PocketMine uses these
// for chat formatting; CurseStone will use them in player-facing messages.
#define MCP_TF_BLACK       '0'
#define MCP_TF_DARK_BLUE   '1'
#define MCP_TF_DARK_GREEN  '2'
#define MCP_TF_DARK_AQUA   '3'
#define MCP_TF_DARK_RED    '4'
#define MCP_TF_DARK_PURPLE '5'
#define MCP_TF_GOLD        '6'
#define MCP_TF_GRAY        '7'
#define MCP_TF_DARK_GRAY   '8'
#define MCP_TF_BLUE        '9'
#define MCP_TF_GREEN       'a'
#define MCP_TF_AQUA        'b'
#define MCP_TF_RED         'c'
#define MCP_TF_LIGHT_PURPLE 'd'
#define MCP_TF_YELLOW      'e'
#define MCP_TF_WHITE       'f'

#define MCP_TF_OBFUSCATED  'k'
#define MCP_TF_BOLD        'l'
#define MCP_TF_STRIKETHROUGH 'm'
#define MCP_TF_UNDERLINE   'n'
#define MCP_TF_ITALIC      'o'
#define MCP_TF_RESET       'r'

#define MCP_TF_SECTION     "\xc2\xa7"   // UTF-8 sequence for §

// Returns the ANSI escape for a given Minecraft § code (a..r,0..9), or
// NULL if the code is unknown.
const char* mcp_textformat_to_ansi(char code);

// In-place replaces § codes in `out` with ANSI sequences. Output buffer
// must be large enough (worst case 9x input length). Returns true on
// success; false if buffer too small.
bool mcp_textformat_to_terminal(const char* in, char* out, size_t out_size);

// Strip all §-codes from `in`. Caller frees the result.
char* mcp_textformat_clean(const char* in);

#ifdef __cplusplus
} // extern "C"
#endif
