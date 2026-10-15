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
 * @file PlatformCompat.h
 * @brief Maps the few POSIX file-system calls the C++ sources use onto their Windows equivalents.
 */
#pragma once

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <direct.h>
#include <io.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>

// MSVC's mkdir() takes no mode argument, so the macro drops it.
#define mkdir(path, mode) _mkdir(path)

#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
#endif
#ifndef S_ISREG
#define S_ISREG(m) (((m) & _S_IFMT) == _S_IFREG)
#endif

// _chsize_s() is the Windows counterpart of ftruncate() on a CRT descriptor.
#define ftruncate(fd, len) _chsize_s((fd), (len))
#define fileno _fileno

/** @brief One directory entry returned by readdir(); only d_name is provided. */
struct dirent {
    char d_name[MAX_PATH];
};

/** @brief Directory stream state for the opendir()/readdir()/closedir() shim. */
struct DIR {
    HANDLE handle;
    WIN32_FIND_DATAA data;
    bool first; ///< true until the entry found by opendir() has been returned.
    dirent entry;
};

/**
 * @brief Opens a directory for enumeration.
 * @param path  directory path; must not be null.
 * @return a stream to pass to readdir() and closedir(), or nullptr if the
 *         directory cannot be opened or is empty of entries.
 */
inline DIR* opendir(const char* path) {
    std::string pattern(path);
    pattern += "\\*";
    DIR* d = new DIR();
    d->handle = FindFirstFileA(pattern.c_str(), &d->data);
    if (d->handle == INVALID_HANDLE_VALUE) {
        delete d;
        return nullptr;
    }
    d->first = true;
    return d;
}

/**
 * @brief Returns the next entry of the directory stream.
 * @param d  stream returned by opendir(); must not be null.
 * @return the next entry, or nullptr at the end of the directory.
 * @note The returned pointer stays valid until the next readdir() or closedir() call.
 */
inline dirent* readdir(DIR* d) {
    if (d->first) {
        d->first = false;
    } else if (!FindNextFileA(d->handle, &d->data)) {
        return nullptr;
    }
    std::strncpy(d->entry.d_name, d->data.cFileName, MAX_PATH - 1);
    d->entry.d_name[MAX_PATH - 1] = '\0';
    return &d->entry;
}

/**
 * @brief Closes a directory stream and frees it.
 * @param d  stream returned by opendir(); must not be used afterwards.
 * @return 0.
 */
inline int closedir(DIR* d) {
    FindClose(d->handle);
    delete d;
    return 0;
}

#else

#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#endif
