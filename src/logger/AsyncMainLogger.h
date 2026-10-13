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
// AsyncMainLogger.h — async logger with background pthread worker.
//
// Mirrors Nukkit's MainLogger pattern:
//   - info()/warn()/error() only push to a ring buffer + signal condvar
//   - A background pthread does the actual I/O (format, color, fputs)
//   - Batch writes in 4KB blocks, force-flush every 25 seconds
//
// The struct has `mcp_logger_t vtable` as its first field, so it's
// layout-compatible with `mcp_main_logger_t*` — the C++ wrapper
// `utils::MainLogger::get()` works without changes.
//
// Thread model:
//   Producer (tick thread):  lock → push entry → signal → unlock
//   Consumer (worker thread): lock → wait(cond, 25s) → drain → unlock → I/O
//
// When the ring is full, the entry is dropped and `dropped_count` is
// incremented. On the next drain, "[LOG DROPPED xN]" is emitted first.
#pragma once

#include "../utils/Logger.h"
#include <stdbool.h>
#include <stdio.h>
#include <pthread.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Ring buffer dimensions.
#define ASYNC_LOG_RING_SIZE  8192   // entries
#define ASYNC_LOG_LINE_MAX   1024   // max chars per raw message
#define ASYNC_LOG_BATCH_SIZE 4096   // batch write block size
#define ASYNC_LOG_FLUSH_MS   25000  // force flush interval (25 s)

// One entry in the ring buffer.
typedef struct {
    char            text[ASYNC_LOG_LINE_MAX];  // raw message (no timestamp)
    mcp_log_level_t level;
    uint16_t        len;                        // strlen(text)
} async_log_entry_t;

// The async logger singleton. First field is the vtable so that
// `&g_async.vtable` is the same pointer as `(mcp_logger_t*)&g_async`.
typedef struct mcp_async_logger {
    mcp_logger_t    vtable;                     // must be first

    // Ring buffer (single-producer-safe via mutex, but head/tail are
    // also volatile for the worker's lock-free peek).
    async_log_entry_t ring[ASYNC_LOG_RING_SIZE];
    volatile uint32_t head;                     // next write slot
    volatile uint32_t tail;                     // next read slot
    volatile uint32_t count;                    // entries in ring

    // Synchronization.
    pthread_mutex_t mtx;
    pthread_cond_t  cond;
    pthread_t       worker;
    bool            worker_started;

    // I/O targets.
    FILE*           log_file;
    bool            colored;

    // Lifecycle.
    volatile bool   running;
    volatile bool   shutdown_requested;

    // Stats.
    volatile uint64_t dropped_count;             // entries dropped (ring full)
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

#ifdef __cplusplus
} // extern "C"
#endif
