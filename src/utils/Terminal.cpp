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
// Terminal.cpp — tty detection.
#include "Terminal.h"

#include <stdbool.h>

#ifdef _WIN32
#include <io.h>
#define IS_TTY_FD(fd) _isatty(fd)
#else
#include <unistd.h>
#define IS_TTY_FD(fd) isatty(fd)
#endif

static bool g_color_enabled = false;

void mcp_terminal_init(bool force_color) {
    g_color_enabled = force_color || (IS_TTY_FD(1) != 0);
}

bool mcp_terminal_has_color(void) {
    return g_color_enabled;
}
