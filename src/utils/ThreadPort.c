/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_|_|\___|

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
// ThreadPort.c — POSIX (pthreads) and Windows (Win32) backends for ThreadPort.h.
//
// One header, two backends selected at compile time:
//   - POSIX: pthread_mutex/pthread_cond (relative timed wait built from an
//     absolute CLOCK_REALTIME deadline) and pthread_create.
//   - Windows: CRITICAL_SECTION, CONDITION_VARIABLE (SleepConditionVariableCS
//     already takes a relative millisecond timeout) and CreateThread.
// The public API and the semantics are identical on both platforms.

// POSIX visibility for clock_gettime() and pthread_cond_timedwait() under
// strict -std=c2x / C17 (MSVC never takes this branch).
#ifndef _WIN32
#  ifndef _POSIX_C_SOURCE
#    define _POSIX_C_SOURCE 200809L
#  endif
#endif

#include "ThreadPort.h"

#ifndef _WIN32
#  include <errno.h>
#  include <time.h>
#endif

#ifdef _WIN32

/* ---- Windows backend ---- */

bool mcp_mutex_init(mcp_mutex_t* m) {
    InitializeCriticalSection(&m->handle);
    return true;
}

void mcp_mutex_destroy(mcp_mutex_t* m) {
    DeleteCriticalSection(&m->handle);
}

void mcp_mutex_lock(mcp_mutex_t* m) {
    EnterCriticalSection(&m->handle);
}

void mcp_mutex_unlock(mcp_mutex_t* m) {
    LeaveCriticalSection(&m->handle);
}

bool mcp_cond_init(mcp_cond_t* c) {
    InitializeConditionVariable(&c->handle);
    return true;
}

void mcp_cond_destroy(mcp_cond_t* c) {
    // Win32 condition variables are stateless and need no teardown.
    (void)c;
}

void mcp_cond_signal(mcp_cond_t* c) {
    WakeConditionVariable(&c->handle);
}

bool mcp_cond_wait(mcp_cond_t* c, mcp_mutex_t* m, uint32_t timeout_ms) {
    DWORD ms = (timeout_ms == MCP_WAIT_FOREVER) ? INFINITE : (DWORD)timeout_ms;
    // TRUE = signalled, FALSE = timeout or error; both are acceptable here.
    return SleepConditionVariableCS(&c->handle, &m->handle, ms) != FALSE;
}

// Trampoline: Win32 threads take LPVOID, our API passes the handle itself
// so fn/arg are read without any dynamic allocation.
static DWORD WINAPI thread_entry(LPVOID param) {
    mcp_thread_t* t = (mcp_thread_t*)param;
    t->fn(t->arg);
    return 0;
}

bool mcp_thread_start(mcp_thread_t* t, mcp_thread_fn fn, void* arg) {
    t->fn   = fn;
    t->arg  = arg;
    t->handle = CreateThread(NULL, 0, thread_entry, t, 0, NULL);
    return t->handle != NULL;
}

void mcp_thread_join(mcp_thread_t* t) {
    if (t->handle) {
        WaitForSingleObject(t->handle, INFINITE);
        CloseHandle(t->handle);
        t->handle = NULL;
    }
}

#else

/* ---- POSIX backend ---- */

bool mcp_mutex_init(mcp_mutex_t* m) {
    return pthread_mutex_init(&m->handle, NULL) == 0;
}

void mcp_mutex_destroy(mcp_mutex_t* m) {
    pthread_mutex_destroy(&m->handle);
}

void mcp_mutex_lock(mcp_mutex_t* m) {
    pthread_mutex_lock(&m->handle);
}

void mcp_mutex_unlock(mcp_mutex_t* m) {
    pthread_mutex_unlock(&m->handle);
}

bool mcp_cond_init(mcp_cond_t* c) {
    return pthread_cond_init(&c->handle, NULL) == 0;
}

void mcp_cond_destroy(mcp_cond_t* c) {
    pthread_cond_destroy(&c->handle);
}

void mcp_cond_signal(mcp_cond_t* c) {
    pthread_cond_signal(&c->handle);
}

bool mcp_cond_wait(mcp_cond_t* c, mcp_mutex_t* m, uint32_t timeout_ms) {
    if (timeout_ms == MCP_WAIT_FOREVER) {
        return pthread_cond_wait(&c->handle, &m->handle) == 0;
    }

    // pthread_cond_timedwait() takes an ABSOLUTE deadline; convert the
    // relative timeout now so the API stays portable.
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec  += (time_t)(timeout_ms / 1000u);
    ts.tv_nsec += (long)(timeout_ms % 1000u) * 1000000L;
    if (ts.tv_nsec >= 1000000000L) {
        ts.tv_sec  += 1;
        ts.tv_nsec -= 1000000000L;
    }
    return pthread_cond_timedwait(&c->handle, &m->handle, &ts) != ETIMEDOUT;
}

static void* thread_entry(void* param) {
    mcp_thread_t* t = (mcp_thread_t*)param;
    t->fn(t->arg);
    return NULL;
}

bool mcp_thread_start(mcp_thread_t* t, mcp_thread_fn fn, void* arg) {
    t->fn   = fn;
    t->arg  = arg;
    return pthread_create(&t->handle, NULL, thread_entry, t) == 0;
}

void mcp_thread_join(mcp_thread_t* t) {
    pthread_join(t->handle, NULL);
}

#endif
