// SPDX-License-Identifier: LGPL-3.0-or-later
// MainLogger — concrete logger that writes to stdout (with ANSI colors)
// and optionally to a log file. Pure C, but exposes a mcp_logger_t
// vtable so C++ callers can use it polymorphically.
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
