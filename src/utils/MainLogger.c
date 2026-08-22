// SPDX-License-Identifier: LGPL-3.0-or-later
// MainLogger implementation. Pure C.
#include "MainLogger.h"
#include "Terminal.h"
#include "TextFormat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>

static mcp_main_logger_t g_logger;
static bool g_logger_initialized = false;

static void format_timestamp(char* buf, size_t bufsz) {
    time_t t = time(NULL);
    struct tm tm;
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    strftime(buf, bufsz, "%H:%M:%S", &tm);
}

static void emit_line(mcp_main_logger_t* self, mcp_log_level_t lvl,
                      const char* msg) {
    char ts[16];
    format_timestamp(ts, sizeof(ts));
    const char* lvl_name = mcp_log_level_name(lvl);
    // Build a colored terminal line + a plain line for the file.
    char plain[1024];
    int n = snprintf(plain, sizeof(plain), "[%s] [%s] %s\n",
                     ts, lvl_name, msg ? msg : "");
    if (n < 0) return;

    if (self->colored) {
        char ansi[2048];
        // Color the level name according to severity.
        const char* lvl_color = "";
        switch (lvl) {
            case MCP_LOG_DEBUG:    lvl_color = MCP_TERM_BBLACK;  break;
            case MCP_LOG_INFO:     lvl_color = MCP_TERM_BGREEN;  break;
            case MCP_LOG_NOTICE:   lvl_color = MCP_TERM_BCYAN;   break;
            case MCP_LOG_WARN:     lvl_color = MCP_TERM_BYELLOW; break;
            case MCP_LOG_ERROR:    lvl_color = MCP_TERM_BRED;    break;
            case MCP_LOG_CRITICAL: lvl_color = MCP_TERM_BRED;    break;
            case MCP_LOG_EMERGENCY:lvl_color = MCP_TERM_BRED;    break;
        }
        char colored[2048];
        snprintf(colored, sizeof(colored), "[%s] [%s%s%s] %s\n",
                 ts, lvl_color, lvl_name, MCP_TERM_RESET,
                 msg ? msg : "");
        // Replace § codes in msg with ANSI sequences for player chat etc.
        if (!mcp_textformat_to_terminal(colored, ansi, sizeof(ansi))) {
            // Conversion failed: fall back to the plain line.
            fputs(plain, stdout);
        } else {
            fputs(ansi, stdout);
        }
    } else {
        fputs(plain, stdout);
    }
    fflush(stdout);

    if (self->log_file) {
        fputs(plain, self->log_file);
        fflush(self->log_file);
    }
}

static void log_fn(void* self, mcp_log_level_t lvl, const char* msg) {
    (void)self;
    emit_line(&g_logger, lvl, msg);
}
static void debug_fn(void* self, const char* msg)    { emit_line((mcp_main_logger_t*)self, MCP_LOG_DEBUG, msg); }
static void info_fn(void* self, const char* msg)     { emit_line((mcp_main_logger_t*)self, MCP_LOG_INFO, msg); }
static void notice_fn(void* self, const char* msg)   { emit_line((mcp_main_logger_t*)self, MCP_LOG_NOTICE, msg); }
static void warn_fn(void* self, const char* msg)     { emit_line((mcp_main_logger_t*)self, MCP_LOG_WARN, msg); }
static void error_fn(void* self, const char* msg)    { emit_line((mcp_main_logger_t*)self, MCP_LOG_ERROR, msg); }
static void critical_fn(void* self, const char* msg){ emit_line((mcp_main_logger_t*)self, MCP_LOG_CRITICAL, msg); }
static void emergency_fn(void* self, const char* msg){ emit_line((mcp_main_logger_t*)self, MCP_LOG_EMERGENCY, msg); }
static void log_exception_fn(void* self, const char* what, const char* where) {
    char buf[1024];
    snprintf(buf, sizeof(buf), "Exception: %s in %s",
             what ? what : "(null)", where ? where : "(null)");
    emit_line((mcp_main_logger_t*)self, MCP_LOG_ERROR, buf);
}
static void shutdown_fn(void* self) {
    mcp_main_logger_t* ml = (mcp_main_logger_t*)self;
    if (ml->log_file) {
        fclose(ml->log_file);
        ml->log_file = NULL;
    }
    ml->shutdown_called = true;
}

void mcp_main_logger_init(const char* log_path, bool colored) {
    if (g_logger_initialized) return;
    mcp_terminal_init(colored);
    memset(&g_logger, 0, sizeof(g_logger));
    g_logger.colored = mcp_terminal_has_color();
    if (log_path) {
        g_logger.log_file = fopen(log_path, "a");
    }
    g_logger.vtable.log          = log_fn;
    g_logger.vtable.debug        = debug_fn;
    g_logger.vtable.info         = info_fn;
    g_logger.vtable.notice       = notice_fn;
    g_logger.vtable.warn         = warn_fn;
    g_logger.vtable.error        = error_fn;
    g_logger.vtable.critical     = critical_fn;
    g_logger.vtable.emergency    = emergency_fn;
    g_logger.vtable.log_exception= log_exception_fn;
    g_logger.vtable.shutdown     = shutdown_fn;
    g_logger.vtable.impl         = &g_logger;
    g_logger_initialized = true;
}

mcp_main_logger_t* mcp_main_logger_get_instance(void) {
    if (!g_logger_initialized) {
        mcp_main_logger_init(NULL, true);
    }
    return &g_logger;
}

void mcp_main_logger_destroy(void) {
    if (!g_logger_initialized) return;
    if (g_logger.log_file) {
        fclose(g_logger.log_file);
        g_logger.log_file = NULL;
    }
    g_logger_initialized = false;
}
