/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_|_|\___|

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
 * @file ThreadPort.h
 * @brief Cross-platform mutex, condition variable and thread handle.
 *
 * POSIX maps 1:1 onto pthreads; Windows/MSVC maps onto Win32
 * CRITICAL_SECTION + CONDITION_VARIABLE + CreateThread, so no pthread
 * header or symbol ever reaches an MSVC translation unit.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN /* keep winsock.h out; winsock2.h comes first elsewhere */
#endif
#ifndef NOMINMAX
#define NOMINMAX /* windows.h must not define min()/max() macros for C++ */
#endif
#include <windows.h>
#else
#include <pthread.h>
#endif


/// Wait argument that blocks until the condition is signalled.
#define MCP_WAIT_FOREVER 0xFFFFFFFFu

/**
 * @brief Names the CALLING thread so tools like htop, perf and crash
 *        dumps show what each thread is instead of a raw TID.
 *
 * POSIX (Linux, Android, macOS): pthread_setname_np(pthread_self(), ...).
 * Windows: SetThreadDescription, resolved dynamically because it only
 * exists on Windows 10 1607+ — on older systems the call is a no-op and
 * the binary still loads. The name is truncated to 15 bytes, the Linux
 * kernel's limit (a 16-byte buffer including the NUL).
 *
 * @param name  descriptive thread name, e.g. "CS-RakLib"; must not be null.
 */
static inline void mcp_set_thread_name(const char* name) {
    char buf[16];
    size_t i = 0;
    for (; name && name[i] != '\0' && i < 15; ++i)
        buf[i] = name[i];
    buf[i] = '\0';
#ifdef _WIN32
    typedef long (__stdcall *SetThreadDescriptionFn)(void*, const unsigned short*);
    SetThreadDescriptionFn fn = (SetThreadDescriptionFn)(void*)GetProcAddress(
        GetModuleHandleW(L"kernel32.dll"), "SetThreadDescription");
    if (!fn)
        return; // Pre-1607 Windows: descriptive names are not available.
    unsigned short wide[16];
    for (size_t j = 0; j <= i; ++j)
        wide[j] = (unsigned short)(unsigned char)buf[j];
    fn(GetCurrentThread(), wide);
#else
    pthread_setname_np(pthread_self(), buf);
#endif
}

/** @brief Entry point of a thread started with mcp_thread_start(). */
typedef void* (*mcp_thread_fn)(void* arg);

/**
 * @brief Recursive-usage-safe mutual exclusion lock.
 *
 * Storage is platform-specific; zero-initialize nothing, always call
 * mcp_mutex_init() before the first lock.
 */
typedef struct mcp_mutex {
#ifdef _WIN32
    CRITICAL_SECTION handle;
#else
    pthread_mutex_t handle;
#endif
} mcp_mutex_t;

/** @brief Condition variable paired with one mcp_mutex_t. */
typedef struct mcp_cond {
#ifdef _WIN32
    CONDITION_VARIABLE handle;
#else
    pthread_cond_t handle;
#endif
} mcp_cond_t;

/** @brief Handle of a running thread, created by mcp_thread_start(). */
typedef struct mcp_thread {
    mcp_thread_fn fn; /**< Thread body; its return value is discarded. */
    void* arg;        /**< Argument forwarded to fn. */
#ifdef _WIN32
    HANDLE handle;
#else
    pthread_t handle;
#endif
} mcp_thread_t;

/**
 * @brief Initializes a mutex.
 * @param m  mutex to initialize; must not be null.
 * @return true on success.
 */
bool mcp_mutex_init(mcp_mutex_t* m);

/**
 * @brief Releases the resources of a mutex.
 * @param m  mutex previously initialized with mcp_mutex_init().
 * @warning No thread may hold the lock when this is called.
 */
void mcp_mutex_destroy(mcp_mutex_t* m);

/**
 * @brief Acquires the mutex, blocking until it is available.
 * @param m  mutex to lock; must not be null.
 */
void mcp_mutex_lock(mcp_mutex_t* m);

/**
 * @brief Releases the mutex.
 * @param m  mutex to unlock; must be held by the calling thread.
 */
void mcp_mutex_unlock(mcp_mutex_t* m);

/**
 * @brief Initializes a condition variable.
 * @param c  condition variable to initialize; must not be null.
 * @return true on success.
 */
bool mcp_cond_init(mcp_cond_t* c);

/**
 * @brief Releases the resources of a condition variable.
 * @param c  condition variable previously initialized with mcp_cond_init().
 */
void mcp_cond_destroy(mcp_cond_t* c);

/**
 * @brief Wakes one waiter blocked on the condition variable.
 * @param c  condition variable to signal; must not be null.
 * @note The caller must hold the paired mutex, matching pthread practice.
 */
void mcp_cond_signal(mcp_cond_t* c);

/**
 * @brief Waits on the condition variable, releasing the paired mutex.
 * @param c           condition variable to wait on.
 * @param m           mutex to release and re-acquire; the caller holds it.
 * @param timeout_ms  relative timeout in milliseconds, or MCP_WAIT_FOREVER.
 * @return true if woken by a signal, false on timeout or error.
 * @note The wait deadline is relative to the call, so it is immune to
 *       wall-clock changes; on Windows it maps to SleepConditionVariableCS().
 */
bool mcp_cond_wait(mcp_cond_t* c, mcp_mutex_t* m, uint32_t timeout_ms);

/**
 * @brief Starts a thread that runs fn(arg).
 * @param t   thread handle to initialize; must not be null.
 * @param fn  thread body; its return value is discarded.
 * @param arg argument forwarded to fn.
 * @return true if the thread was created.
 */
bool mcp_thread_start(mcp_thread_t* t, mcp_thread_fn fn, void* arg);

/**
 * @brief Waits for the thread to finish and releases its handle.
 * @param t  thread to join; started with mcp_thread_start().
 * @warning Safe to call once; a second join is undefined.
 */
void mcp_thread_join(mcp_thread_t* t);

