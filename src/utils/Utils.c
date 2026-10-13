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
// Utils.c — misc helpers. Pure C.
#define _POSIX_C_SOURCE 199309L
#include "Utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#  include <windows.h>
#else
#  include <time.h>
#  include <unistd.h>
#endif

uint64_t mcp_utils_time_ms(void) {
#ifdef _WIN32
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER ul;
    ul.LowPart  = ft.dwLowDateTime;
    ul.HighPart = ft.dwHighDateTime;
    // FILETIME is in 100-nanosecond intervals since 1601-01-01.
    // Convert to Unix epoch (1970-01-01) milliseconds.
    return (uint64_t)((ul.QuadPart - 116444736000000000ULL) / 10000ULL);
#else
    // Use clock_gettime(CLOCK_REALTIME) — POSIX, available on Android API 1+
    // (NOT timespec_get, which only landed in bionic API 29 / Android 10).
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)(ts.tv_nsec / 1000000);
#endif
}

void mcp_utils_sleep_ms(uint32_t ms) {
#ifdef _WIN32
    Sleep(ms);
#else
    struct timespec ts;
    ts.tv_sec  = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
#endif
}

char* mcp_utils_file_slurp(const char* path, size_t* out_size) {
    if (!path) return NULL;
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    if (sz < 0) { fclose(f); return NULL; }
    fseek(f, 0, SEEK_SET);
    char* buf = (char*)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t rd = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[rd] = '\0';
    if (out_size) *out_size = rd;
    return buf;
}

bool mcp_utils_file_write(const char* path, const void* data, size_t len) {
    if (!path || (!data && len)) return false;
    FILE* f = fopen(path, "wb");
    if (!f) return false;
    size_t wr = fwrite(data ? data : "", 1, len, f);
    fclose(f);
    return wr == len;
}

char* mcp_utils_trim(char* s) {
    if (!s) return NULL;
    char* start = s;
    while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n') start++;
    if (*start == '\0') {
        s[0] = '\0';
        return s;
    }
    char* end = start + strlen(start) - 1;
    while (end > start && (*end == ' ' || *end == '\t' ||
                            *end == '\r' || *end == '\n')) *end-- = '\0';
    if (start != s) memmove(s, start, strlen(start) + 1);
    return s;
}

char** mcp_utils_split(const char* s, char delim, size_t* out_count) {
    if (!s) return NULL;
    size_t count = 1;
    for (const char* p = s; *p; ++p) if (*p == delim) ++count;
    char** arr = (char**)malloc(sizeof(char*) * (count + 1));
    if (!arr) return NULL;
    size_t ai = 0;
    const char* start = s;
    for (const char* p = s; ; ++p) {
        if (*p == delim || *p == '\0') {
            size_t len = (size_t)(p - start);
            char* tok = (char*)malloc(len + 1);
            if (!tok) {
                for (size_t i = 0; i < ai; ++i) free(arr[i]);
                free(arr);
                return NULL;
            }
            memcpy(tok, start, len);
            tok[len] = '\0';
            arr[ai++] = tok;
            start = p + 1;
            if (*p == '\0') break;
        }
    }
    arr[ai] = NULL;
    if (out_count) *out_count = ai;
    return arr;
}

void mcp_utils_split_free(char** arr) {
    if (!arr) return;
    for (char** p = arr; *p; ++p) free(*p);
    free(arr);
}

void mcp_utils_human_size(uint64_t bytes, char* out, size_t out_size) {
    if (!out || out_size == 0) return;
    const char* units[] = {"B", "KB", "MB", "GB", "TB", "PB"};
    int ui = 0;
    double d = (double)bytes;
    while (d >= 1024.0 && ui < 5) { d /= 1024.0; ++ui; }
    snprintf(out, out_size, "%.2f %s", d, units[ui]);
}
