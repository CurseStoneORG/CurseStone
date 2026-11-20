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
/**
 * @brief Logs asynchronously through a background worker thread.
 *
 * The console side of the CurseStone Diagnostic Engine. Contract:
 *   - info()/warn()/error()/debug()/raw() only push a pre-stamped entry into
 *     a ring buffer and signal a condvar (lock + memcpy, no I/O).
 *   - The worker thread does all I/O: one write per drain cycle for the
 *     console and one for the log file, so a burst of N lines costs two
 *     syscalls — and the LAST line of the burst is on screen the moment the
 *     producer finishes. There is deliberately NO timed deferral: every drain
 *     ends in a flush, which is why `help` and friends echo instantly.
 *   - Entries whose text contains '\n' are diagnostic blocks (Diagnostic
 *     Engine): written verbatim, without timestamp or level tag. § color
 *     codes inside are converted to ANSI for the console and stripped for
 *     the log file.
 *
 * Output shapes:
 *   console: [17:14:40] INFO  message          (level tags INFO/WARN/ERROR/
 *                                              FATAL/DEBUG, minimalist colors)
 *   file:    [17:14:40] [INFO] message         (classic format, always plain —
 *                                              scripts and tail -f friendly)
 *
 * The struct has `mcp_logger_t vtable` as its first field, so it is
 * layout-compatible with `mcp_main_logger_t*` — the C++ wrapper
 * `utils::MainLogger::get()` works without changes.
 *
 * Thread model:
 *   Producer (tick thread):  lock -> stamp -> push entry -> signal -> unlock
 *   Consumer (worker thread): lock -> wait(cond, 500 ms cap) -> drain -> unlock -> I/O
 *
 * When the ring is full, the entry is dropped and `dropped_count` is
 * incremented. On the next drain, "[LOG DROPPED xN]" is emitted first.
 */
#pragma once

#include "../utils/Logger.h"
#include "../utils/ThreadPort.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>

// Ring buffer dimensions. 2048 entries of 2 KB = ~4 MB of static storage:
// large enough that even a debug-mode packet trace burst never drops lines
// between two drains, small enough for the Android/Termux builds.
#define ASYNC_LOG_RING_SIZE 2048  // entries
#define ASYNC_LOG_LINE_MAX 2048   // max chars per raw message (diagnostic blocks included)
#define ASYNC_LOG_BATCH_SIZE 4096 // batch write block size

// Worker condvar wait cap. Purely a safety net — every drain cycle flushes,
// so under normal operation the worker never actually times out.
#define ASYNC_LOG_POLL_MS 500

// One entry in the ring buffer. `ts` is stamped by the PRODUCER: log lines
// show the moment they happened, not the moment the worker got around to
// formatting them (a burst drained after a busy tick used to carry the
// drain-time timestamp on every line).
typedef struct {
    char ts[16];                   // "%H:%M:%S" stamped at push time
    char text[ASYNC_LOG_LINE_MAX]; // raw message (no timestamp)
    mcp_log_level_t level;
    uint16_t len; // strlen(text)
} async_log_entry_t;

// The async logger singleton. First field is the vtable so that
// `&g_async.vtable` is the same pointer as `(mcp_logger_t*)&g_async`.
typedef struct mcp_async_logger {
    mcp_logger_t vtable; // must be first

    // Ring buffer (single-producer-safe via mutex, but head/tail are
    // also volatile for the worker's lock-free peek).
    async_log_entry_t ring[ASYNC_LOG_RING_SIZE];
    volatile uint32_t head;  // next write slot
    volatile uint32_t tail;  // next read slot
    volatile uint32_t count; // entries in ring

    // Synchronization (portable layer: pthreads on POSIX, Win32 on
    // Windows — see src/utils/ThreadPort.h).
    mcp_mutex_t mtx;
    mcp_cond_t cond;
    mcp_thread_t worker;
    bool worker_started;

    // I/O targets.
    FILE* log_file;
    bool colored;

    // Lifecycle.
    volatile bool running;
    volatile bool shutdown_requested;

    // Stats.
    volatile uint64_t dropped_count; // entries dropped (ring full)
} mcp_async_logger_t;

// Initialize the async logger singleton. `log_path` may be NULL to
// skip file logging. Spawns the worker thread.
void mcp_main_logger_async_init(const char* log_path, bool colored);

// Shutdown: signals the worker, waits for it to drain + join.
// Call once at process exit (after the tick loop has stopped).
void mcp_main_logger_async_destroy(void);

// Get the singleton instance. The vtable is at offset 0, so this
// pointer can be cast to `mcp_logger_t*` directly.
mcp_async_logger_t* mcp_async_logger_get_instance(void);

// Check if the async logger has been initialized.
bool mcp_async_logger_is_initialized(void);
