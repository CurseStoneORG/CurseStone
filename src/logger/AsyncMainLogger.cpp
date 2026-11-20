/*
 _____                     _____ _
/  __ \                   /  ___| |
| | /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

 * CurseStone | Open Source Server Software
 *
 * Copyright (C) 2026 CurseStoneORG
 *
 * Developed by CurseStoneORG.
 * Built for an open and extensible Minecraft server ecosystem.
 *
 * Licensed under the GNU Lesser General Public License v3.0 or later.
 * You may study, modify, and redistribute this software under its license.
 *
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 * Repository: https://github.com/CurseStoneORG/CurseStone
 *
 */
// SPDX-License-Identifier: LGPL-3.0-or-later
// AsyncMainLogger.cpp — async logger implementation on the portable thread
// layer (src/utils/ThreadPort: pthreads on POSIX, Win32 on Windows).
//
// The producer thread only does: stamp timestamp -> lock -> memcpy entry ->
// signal -> unlock. The worker does all the I/O.
//
// Latency contract: EVERY drain cycle ends in a flush of both output
// batches. The producer signals the condvar on each push, so the wakeup is
// immediate and the visible delay of a console command reply is one context
// switch, not a batching window (the old policy held a small batch until a
// 500 ms idle timeout — the "half second lag" on `help`).
//
// Output paths:
//   console (stdout): colored when enabled — dim timestamp, colored level
//     tag, message; § codes converted to ANSI. Plain otherwise.
//   file (server.log): ALWAYS plain; § codes stripped. Line format kept
//     stable ("[ts] [LEVEL] msg") because operators and scripts grep it.
//
// Multi-line entries are Diagnostic Engine blocks and are written verbatim
// (no prefix) through the same two paths.

// Need POSIX visibility (localtime_r) in strict C23 mode.
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "AsyncMainLogger.h"
#include "../utils/Terminal.h"
#include "../utils/TextFormat.h"
#include "../utils/Logger.h"
#include "../utils/DebugFlag.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>

// Singleton
static mcp_async_logger_t g_async;
static bool g_async_initialized = false;
// Once explicitly destroyed (process shutdown), the async logger must never
// be respawned: a lazy re-init would spawn a fresh joinable worker thread
// mid-teardown whose FILE* and mutex are never cleaned up.
static bool g_async_destroyed = false;

// Helpers — timestamp (producer side), level names
static void stamp_timestamp(char* buf, size_t bufsz) {
    time_t t = time(NULL);
    struct tm tm;
#ifdef _WIN32
    // MSVC has no localtime_r(); the C11 localtime_s() plays the same role.
    if (localtime_s(&tm, &t) != 0) {
        buf[0] = '\0';
        return;
    }
#else
    localtime_r(&t, &tm);
#endif
    strftime(buf, bufsz, "%H:%M:%S", &tm);
}

// Console level tags — the Diagnostic Engine's five-level vocabulary.
// Zero-padded to one width so the message column lines up on every line:
// "[17:14:40] INFO  ..." / "[17:14:40] ERROR ...". The log file keeps the
// long-form names (WARNING/CRITICAL/...) via mcp_log_level_name().
static const char* level_tag(mcp_log_level_t lvl) {
    switch (lvl) {
    case MCP_LOG_DEBUG:
        return "DEBUG";
    case MCP_LOG_INFO:
    case MCP_LOG_NOTICE:
        return "INFO ";
    case MCP_LOG_WARN:
        return "WARN ";
    case MCP_LOG_ERROR:
        return "ERROR";
    case MCP_LOG_CRITICAL:
    case MCP_LOG_EMERGENCY:
        return "FATAL";
    default:
        return "INFO ";
    }
}

// Minimalist ANSI palette for the level tags (256-color, soft tones —
// readable on both dark and light terminals; nothing saturated).
static const char* level_tag_color(mcp_log_level_t lvl) {
    switch (lvl) {
    case MCP_LOG_DEBUG:
        return "\x1b[38;5;245m"; // dim gray
    case MCP_LOG_INFO:
    case MCP_LOG_NOTICE:
        return "\x1b[38;5;114m"; // soft green
    case MCP_LOG_WARN:
        return "\x1b[38;5;179m"; // soft amber
    case MCP_LOG_ERROR:
        return "\x1b[38;5;203m"; // soft red
    case MCP_LOG_CRITICAL:
    case MCP_LOG_EMERGENCY:
        return "\x1b[1;38;5;203m"; // bold soft red
    default:
        return "";
    }
}

#define MCP_TERM_DIM "\x1b[38;5;245m"

// Append `text` (len bytes) to `batch`, flushing the batch to `out` first
// when it would overflow. Returns false when the LINE itself cannot fit
// (never expected: batch cap exceeds one line).
static bool batch_append(char* batch, size_t batch_cap, size_t* batch_len, const char* text,
                         size_t len, FILE* out) {
    if (*batch_len + len >= batch_cap) {
        if (*batch_len == 0)
            return false; // single line larger than the batch buffer
        if (out)
            fputs(batch, out);
        *batch_len = 0;
        batch[0] = '\0';
    }
    memcpy(batch + *batch_len, text, len);
    *batch_len += len;
    batch[*batch_len] = '\0';
    return true;
}

// Strip Minecraft § color codes (UTF-8 0xC2 0xA7 + code char). Used for the
// plain path: the log file and non-colored consoles must stay clean.
static size_t strip_section_codes(const char* in, char* out, size_t out_cap) {
    size_t oi = 0;
    for (size_t i = 0; in[i] && oi + 1 < out_cap; ++i) {
        if ((unsigned char)in[i] == 0xC2 && (unsigned char)in[i + 1] == 0xA7) {
            i += 2; // skip the pair plus the code char
            if (!in[i])
                break;
            continue;
        }
        out[oi++] = in[i];
    }
    out[oi] = '\0';
    return oi;
}

// One entry -> two batch lines. Multi-line text is a Diagnostic Engine
// block: verbatim, no prefix, its own embedded layout and § colors.
static void format_entry(mcp_async_logger_t* self, const async_log_entry_t* entry, char* tty_batch,
                         size_t tty_cap, size_t* tty_len, char* file_batch, size_t file_cap,
                         size_t* file_len) {
    const char* msg = entry->text[0] ? entry->text : "";
    const bool block = strchr(msg, '\n') != NULL;

    // ---- plain text (file); the console uses its own shapes below ----
    char plain[ASYNC_LOG_LINE_MAX + 64];
    size_t plain_len;
    if (block) {
        plain_len = strip_section_codes(msg, plain, sizeof(plain) - 1);
        if (plain_len && plain[plain_len - 1] != '\n' && plain_len + 1 < sizeof(plain)) {
            plain[plain_len++] = '\n';
            plain[plain_len] = '\0';
        }
    } else {
        // § codes may ride ordinary messages (chat echoes); strip them.
        char clean[ASYNC_LOG_LINE_MAX];
        strip_section_codes(msg, clean, sizeof(clean));
        int n = snprintf(plain, sizeof(plain), "[%s] [%s] %s\n", entry->ts,
                         mcp_log_level_name(entry->level), clean);
        if (n < 0)
            return;
        if ((size_t)n >= sizeof(plain))
            n = (int)(sizeof(plain) - 1);
        plain_len = (size_t)n;
    }
    if (self->log_file)
        batch_append(file_batch, file_cap, file_len, plain, plain_len, self->log_file);

    // ---- console: colored when enabled, tag-plain otherwise ----
    // Exactly ONE line per entry reaches stdout. The non-colored console
    // keeps the Diagnostic Engine's tag shape ("[ts] INFO  msg"), not the
    // file's bracketed shape.
    if (!self->colored) {
        char tty_plain[ASYNC_LOG_LINE_MAX + 64];
        size_t tty_plain_len;
        if (block) {
            memcpy(tty_plain, plain, plain_len);
            tty_plain[plain_len] = '\0';
            tty_plain_len = plain_len;
        } else {
            char clean[ASYNC_LOG_LINE_MAX];
            strip_section_codes(msg, clean, sizeof(clean));
            int n = snprintf(tty_plain, sizeof(tty_plain), "[%s] %s %s\n", entry->ts,
                             level_tag(entry->level), clean);
            if (n < 0)
                return;
            if ((size_t)n >= sizeof(tty_plain))
                n = (int)(sizeof(tty_plain) - 1);
            tty_plain_len = (size_t)n;
        }
        batch_append(tty_batch, tty_cap, tty_len, tty_plain, tty_plain_len, stdout);
        return;
    }
    char colored[ASYNC_LOG_LINE_MAX + 192];
    size_t c_len;
    if (block) {
        c_len = (size_t)snprintf(colored, sizeof(colored), "%s\n", msg);
        if (c_len >= sizeof(colored))
            c_len = sizeof(colored) - 1;
    } else {
        c_len = (size_t)snprintf(colored, sizeof(colored), MCP_TERM_DIM "[%s]\x1b[0m %s%s\x1b[0m %s\n",
                                 entry->ts, level_tag_color(entry->level), level_tag(entry->level),
                                 msg);
        if (c_len >= sizeof(colored))
            c_len = sizeof(colored) - 1;
    }
    // § codes -> ANSI (mcp_textformat_to_terminal appends a trailing reset).
    char ansi[sizeof(colored) + 256];
    if (!mcp_textformat_to_terminal(colored, ansi, sizeof(ansi))) {
        memcpy(ansi, colored, c_len);
        ansi[c_len] = '\0';
    }
    size_t ansi_len = strlen(ansi);
    if (ansi_len && ansi[ansi_len - 1] != '\n' && ansi_len + 1 < sizeof(ansi)) {
        ansi[ansi_len++] = '\n';
        ansi[ansi_len] = '\0';
    }
    batch_append(tty_batch, tty_cap, tty_len, ansi, ansi_len, stdout);
}

static void flush_batches(mcp_async_logger_t* self, char* tty_batch, size_t tty_len,
                          char* file_batch, size_t file_len) {
    if (self->log_file && file_len > 0) {
        fputs(file_batch, self->log_file);
        fflush(self->log_file);
    }
    if (tty_len > 0) {
        fputs(tty_batch, stdout);
        fflush(stdout);
    }
}

// Ring buffer push (called by tick thread via vtable functions)
static void async_push(mcp_async_logger_t* self, mcp_log_level_t lvl, const char* msg) {
    if (!self || !self->running)
        return;

    uint16_t len = 0;
    if (msg) {
        len = (uint16_t)strlen(msg);
        if (len >= ASYNC_LOG_LINE_MAX)
            len = ASYNC_LOG_LINE_MAX - 1;
    }

    char ts[16];
    stamp_timestamp(ts, sizeof(ts)); // producer-side: the line shows WHEN it happened

    mcp_mutex_lock(&self->mtx);

    if (self->count >= ASYNC_LOG_RING_SIZE) {
        // Ring full — drop this entry.
        self->dropped_count++;
        mcp_mutex_unlock(&self->mtx);
        return;
    }

    uint32_t idx = self->head;
    async_log_entry_t* entry = &self->ring[idx];
    memcpy(entry->ts, ts, sizeof(ts));
    if (msg && len > 0) {
        memcpy(entry->text, msg, len);
    }
    entry->text[len] = '\0';
    entry->level = lvl;
    entry->len = len;

    self->head = (idx + 1) % ASYNC_LOG_RING_SIZE;
    self->count++;

    mcp_cond_signal(&self->cond);
    mcp_mutex_unlock(&self->mtx);
}

// Vtable trampoline functions
static void async_log_fn(void* self, mcp_log_level_t lvl, const char* msg) {
    async_push((mcp_async_logger_t*)self, lvl, msg);
}
static void async_debug_fn(void* self, const char* msg) {
    // debug() lines are part of the scattered debug output the
    // debug-mode switch owns (cursestone.yml settings.debug-mode).
    // Without the switch they printed on every boot by default and the
    // console became unreadable; with it they are collected behind ONE
    // key like the [NET]/[PKT] traces. Errors and warnings are NOT
    // affected — they always print.
    if (!mcp_debug_enabled())
        return;
    async_push((mcp_async_logger_t*)self, MCP_LOG_DEBUG, msg);
}
static void async_info_fn(void* self, const char* msg) {
    async_push((mcp_async_logger_t*)self, MCP_LOG_INFO, msg);
}
static void async_notice_fn(void* self, const char* msg) {
    async_push((mcp_async_logger_t*)self, MCP_LOG_NOTICE, msg);
}
static void async_warn_fn(void* self, const char* msg) {
    async_push((mcp_async_logger_t*)self, MCP_LOG_WARN, msg);
}
static void async_error_fn(void* self, const char* msg) {
    async_push((mcp_async_logger_t*)self, MCP_LOG_ERROR, msg);
}
static void async_critical_fn(void* self, const char* msg) {
    async_push((mcp_async_logger_t*)self, MCP_LOG_CRITICAL, msg);
}
static void async_emergency_fn(void* self, const char* msg) {
    async_push((mcp_async_logger_t*)self, MCP_LOG_EMERGENCY, msg);
}
static void async_log_exception_fn(void* self, const char* what, const char* where) {
    char buf[1024];
    snprintf(buf, sizeof(buf), "Exception: %s in %s", what ? what : "(null)",
             where ? where : "(null)");
    async_push((mcp_async_logger_t*)self, MCP_LOG_ERROR, buf);
}
static void async_raw_fn(void* self, const char* msg) {
    async_push((mcp_async_logger_t*)self, MCP_LOG_INFO, msg); // multi-line -> verbatim path
}
static void async_shutdown_fn(void* self) {
    // Defer to mcp_main_logger_async_destroy — the caller (CurseStone.cpp)
    // handles the actual join. This is a no-op so the vtable is valid.
    (void)self;
}

// Worker thread — drains the ring buffer and writes to stdout + file
static void* async_worker_fn(void* arg) {
    mcp_async_logger_t* self = (mcp_async_logger_t*)arg;

    // Per-target batch buffers: one fputs per drain cycle per target.
    char tty_batch[ASYNC_LOG_BATCH_SIZE + ASYNC_LOG_LINE_MAX + 320];
    char file_batch[ASYNC_LOG_BATCH_SIZE + ASYNC_LOG_LINE_MAX + 64];
    size_t tty_len = 0, file_len = 0;
    tty_batch[0] = '\0';
    file_batch[0] = '\0';

    while (1) {
        mcp_mutex_lock(&self->mtx);

        // Wait for entries or shutdown. Every drain flushes, so the timeout
        // only ever fires when there is nothing to do.
        while (self->count == 0 && !self->shutdown_requested) {
            // Relative timeout, so a wall-clock step cannot stall the wait.
            if (!mcp_cond_wait(&self->cond, &self->mtx, ASYNC_LOG_POLL_MS)) {
                break; // timed out
            }
        }

        if (self->count == 0) {
            bool done = self->shutdown_requested;
            mcp_mutex_unlock(&self->mtx);
            if (done)
                break;
            continue;
        }

        // Check for dropped logs.
        uint64_t dropped = self->dropped_count;
        if (dropped > 0) {
            self->dropped_count = 0;
        }

        // Drain all pending entries — copied out under the lock so the
        // producer never waits on formatting.
        async_log_entry_t local_entries[256];
        uint32_t n_drained = 0;
        while (self->count > 0 && n_drained < 256) {
            uint32_t idx = self->tail;
            local_entries[n_drained] = self->ring[idx];
            self->tail = (idx + 1) % ASYNC_LOG_RING_SIZE;
            self->count--;
            n_drained++;
        }

        bool do_shutdown = self->shutdown_requested;
        mcp_mutex_unlock(&self->mtx);

        // Emit "[LOG DROPPED xN]" first so it precedes the batch it belongs to.
        if (dropped > 0) {
            async_log_entry_t drop_entry;
            snprintf(drop_entry.ts, sizeof(drop_entry.ts), "%s", "");
            stamp_timestamp(drop_entry.ts, sizeof(drop_entry.ts));
            snprintf(drop_entry.text, sizeof(drop_entry.text), "[LOG DROPPED x%llu]",
                     (unsigned long long)dropped);
            drop_entry.level = MCP_LOG_WARN;
            drop_entry.len = (uint16_t)strlen(drop_entry.text);
            format_entry(self, &drop_entry, tty_batch, sizeof(tty_batch), &tty_len, file_batch,
                         sizeof(file_batch), &file_len);
        }

        for (uint32_t i = 0; i < n_drained; ++i) {
            format_entry(self, &local_entries[i], tty_batch, sizeof(tty_batch), &tty_len,
                         file_batch, sizeof(file_batch), &file_len);
        }

        // Latency contract: every drain cycle ends in a flush — both targets,
        // every time. The 4 KB batching still collapses a burst into one
        // write per target, but nothing is ever held back waiting for more.
        flush_batches(self, tty_batch, tty_len, file_batch, file_len);
        tty_len = 0;
        file_len = 0;
        tty_batch[0] = '\0';
        file_batch[0] = '\0';

        if (do_shutdown)
            break;
    }

    return NULL;
}

// Public API
void mcp_main_logger_async_init(const char* log_path, bool colored) {
    if (g_async_initialized) {
        // The singleton was lazily initialized by early logging (before
        // Server::initLogger) without a log file. Attach the file now when
        // one arrives — otherwise a first run would never produce server.log —
        // and adopt the config-informed color mode.
        if (log_path && !g_async.log_file) {
            mcp_mutex_lock(&g_async.mtx);
            g_async.log_file = fopen(log_path, "a");
            g_async.colored = mcp_terminal_has_color();
            mcp_mutex_unlock(&g_async.mtx);
        }
        return;
    }
    if (g_async_destroyed)
        return;

    mcp_terminal_init(colored);

    memset(&g_async, 0, sizeof(g_async));
    g_async.colored = mcp_terminal_has_color();
    g_async.running = true;

    if (log_path) {
        g_async.log_file = fopen(log_path, "a");
    }

    // Set up the vtable.
    g_async.vtable.log = async_log_fn;
    g_async.vtable.debug = async_debug_fn;
    g_async.vtable.info = async_info_fn;
    g_async.vtable.notice = async_notice_fn;
    g_async.vtable.warn = async_warn_fn;
    g_async.vtable.error = async_error_fn;
    g_async.vtable.critical = async_critical_fn;
    g_async.vtable.emergency = async_emergency_fn;
    g_async.vtable.log_exception = async_log_exception_fn;
    g_async.vtable.shutdown = async_shutdown_fn;
    g_async.vtable.raw = async_raw_fn;
    g_async.vtable.impl = &g_async;

    // Initialize sync primitives.
    mcp_mutex_init(&g_async.mtx);
    mcp_cond_init(&g_async.cond);

    // Spawn worker thread.
    g_async.worker_started = mcp_thread_start(&g_async.worker, async_worker_fn, &g_async);

    g_async_initialized = true;
}

void mcp_main_logger_async_destroy(void) {
    if (!g_async_initialized)
        return;

    // Signal shutdown.
    mcp_mutex_lock(&g_async.mtx);
    g_async.shutdown_requested = true;
    g_async.running = false;
    mcp_cond_signal(&g_async.cond);
    mcp_mutex_unlock(&g_async.mtx);

    // Wait for worker to drain + exit.
    if (g_async.worker_started) {
        mcp_thread_join(&g_async.worker);
        g_async.worker_started = false;
    }

    // Close file.
    if (g_async.log_file) {
        fflush(g_async.log_file);
        fclose(g_async.log_file);
        g_async.log_file = NULL;
    }

    // Cleanup sync.
    mcp_mutex_destroy(&g_async.mtx);
    mcp_cond_destroy(&g_async.cond);

    g_async_initialized = false;
    g_async_destroyed = true;
}

mcp_async_logger_t* mcp_async_logger_get_instance(void) {
    if (!g_async_initialized && !g_async_destroyed) {
        mcp_main_logger_async_init(NULL, false); // auto-detect, see MainLogger.cpp
    }
    return &g_async;
}

bool mcp_async_logger_is_initialized(void) {
    return g_async_initialized;
}
