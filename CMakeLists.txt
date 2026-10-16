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

cmake_minimum_required(VERSION 3.20)
project(CurseStone VERSION 1.0.2 LANGUAGES C CXX)

# ---- Language standards ----
# C20/C23 (c2x) for RakLib / NBT / Binary / math / chunk data structs.
# C++20 for game logic.
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- Build type defaults ----
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ---- Warning level ----
add_compile_options(
    $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-Wall>
    $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-Wextra>
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall>
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wextra>
    $<$<CXX_COMPILER_ID:MSVC>:/W3>
)

# ---- Platform detection ----
if(ANDROID)
    include(${CMAKE_SOURCE_DIR}/cmake/Android.cmake)
elseif(WIN32)
    include(${CMAKE_SOURCE_DIR}/cmake/Windows.cmake)
endif()

# ---- Sources ----
file(GLOB_RECURSE SOURCES_C CONFIGURE_DEPENDS
    src/**/*.c
    src/*.c
)

file(GLOB_RECURSE SOURCES_CXX CONFIGURE_DEPENDS
    src/**/*.cpp
    src/*.cpp
)

add_executable(CurseStone ${SOURCES_C} ${SOURCES_CXX})

target_include_directories(CurseStone PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

# ---- Lua 5.4 ----
# Lua is bundled in third_party/lua and compiled as a static library.
# It remains available for the server's plugin system.
file(GLOB LUA_SOURCES
    ${CMAKE_SOURCE_DIR}/third_party/lua/*.c
)

list(FILTER LUA_SOURCES EXCLUDE REGEX ".*/(lua|luac)\\.c$")

add_library(lua STATIC ${LUA_SOURCES})

set_target_properties(lua PROPERTIES
    C_STANDARD 99
    C_EXTENSIONS ON
)

target_include_directories(lua PUBLIC
    ${CMAKE_SOURCE_DIR}/third_party/lua
)

if(NOT WIN32)
    target_compile_definitions(lua PRIVATE LUA_USE_POSIX)
endif()

target_compile_options(lua PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-w>
    $<$<C_COMPILER_ID:MSVC>:/w>
)

# ---- Threading ----
# POSIX builds need pthreads via Threads::Threads. The Windows/MSVC build
# uses the Win32 backend of src/utils/ThreadPort.c (kernel32, linked by
# default), so no extra thread library is required there.
find_package(Threads REQUIRED)

target_link_libraries(CurseStone PRIVATE
    Threads::Threads
    lua
)

# ---- Backtraces with symbols in crash dumps ----
set_target_properties(CurseStone PROPERTIES
    ENABLE_EXPORTS ON
)

if(APPLE)
    target_link_options(CurseStone PRIVATE -Wl,-export_dynamic)
endif()

# ---- zlib (packet compression) ----
find_package(ZLIB REQUIRED)

target_link_libraries(CurseStone PRIVATE ZLIB::ZLIB)

# ---- Optional LevelDB ----
include(${CMAKE_SOURCE_DIR}/cmake/FindLevelDB.cmake)

if(LEVELDB_FOUND)
    target_link_libraries(CurseStone PRIVATE ${LEVELDB_LIBRARIES})

    target_include_directories(CurseStone PRIVATE
        ${LEVELDB_INCLUDE_DIRS}
    )

    target_compile_definitions(CurseStone PRIVATE
        CURSESTONE_LEVELDB=1
    )
endif()

# ---- POSIX dl ----
if(UNIX)
    target_link_libraries(CurseStone PRIVATE dl)
endif()

# ---- Android NDK specifics ----
if(ANDROID)
    target_link_libraries(CurseStone PRIVATE log android)
endif()
