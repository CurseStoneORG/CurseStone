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
 * @brief Writes log records to stdout (with ANSI colors) and
 * optionally to a log file.
 *
 * Pure C, but exposes a mcp_logger_t vtable so C++ callers can use it
 * polymorphically.
 */
#pragma once

#include "Logger.h"
#include <stdbool.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mcp_main_logger {
    mcp_logger_t vtable;     // must be first
    FILE*        log_file;
    bool         colored;
    bool         shutdown_called;
    // Implicit singleton: mcp_main_logger_get_instance() returns &g_logger
} mcp_main_logger_t;

// Singleton accessors (mirror PocketMine's MainLogger::getInstance()).
mcp_main_logger_t* mcp_main_logger_get_instance(void);

// Initialize the singleton. Call once at startup. `log_path` may be NULL
// to skip file logging.
void mcp_main_logger_init(const char* log_path, bool colored);

// Cleanup. Call once at shutdown.
void mcp_main_logger_destroy(void);

#ifdef __cplusplus
} // extern "C"
#endif
