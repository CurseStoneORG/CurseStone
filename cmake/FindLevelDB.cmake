#[[
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
]]

# cmake/FindLevelDB.cmake — locate Google's LevelDB library (optional).
# Sets: LEVELDB_FOUND, LEVELDB_INCLUDE_DIRS, LEVELDB_LIBRARIES.

find_path(LEVELDB_INCLUDE_DIR NAMES leveldb/db.h HINTS ENV LEVELDB_ROOT PATH_SUFFIXES include)
find_library(LEVELDB_LIBRARY  NAMES leveldb       HINTS ENV LEVELDB_ROOT PATH_SUFFIXES lib lib64)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(LevelDB DEFAULT_MSG LEVELDB_LIBRARY LEVELDB_INCLUDE_DIR)

if(LEVELDB_FOUND)
    set(LEVELDB_INCLUDE_DIRS ${LEVELDB_INCLUDE_DIR})
    set(LEVELDB_LIBRARIES    ${LEVELDB_LIBRARY})
    mark_as_advanced(LEVELDB_INCLUDE_DIR LEVELDB_LIBRARY)
endif()
