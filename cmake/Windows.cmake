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

# cmake/Windows.cmake — Windows (MSVC + MinGW) build config
# This file is included only when WIN32 is true.

if(MSVC)
    # MSVC: use UTF-8 source / execution charset, suppress secure-CRT noise
    add_compile_options(/utf-8 /D_CRT_SECURE_NO_WARNINGS /D_WINSOCK_DEPRECATED_NO_WARNINGS)
    # Winsock2 needs to come before windows.h — keep winsock.h out globally.
    # Directory scope: the CurseStone target is created AFTER this file is
    # included, so target_* commands here would fail with "target not found"
    # (and they referenced a stale software name).
    add_compile_definitions(WIN32_LEAN_AND_MEAN _WINSOCKAPI_=0)
    # MSVC only declares M_PI and the other <cmath> constants when this is defined.
    add_compile_definitions(_USE_MATH_DEFINES)
else()
    # MinGW: enable posix threads, win32 mutexes are flaky
    add_compile_options(-mthreads)
endif()

# Link winsock2 + ws2_32 (RakLib UDP socket). link_libraries() applies to
# every target created later in the top-level CMakeLists, i.e. CurseStone.
link_libraries(ws2_32 wsock32)

message(STATUS "Windows build: ${CMAKE_CXX_COMPILER_ID}")
