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
// ServerInstance.h — opaque interface that the RakLib worker thread uses
// to talk back to the main thread (the main thread implements this).
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>


typedef struct mcp_server_instance {
    void* impl; // C++ side casts to a concrete class
    // Called when a new client completes the RakNet handshake.
    void (*on_open_session)(void* impl, const char* ip, uint16_t port, int64_t client_id);
    // Called when an established client's RakNet session closes.
    void (*on_close_session)(void* impl, const char* ip, uint16_t port, const char* reason);
    // Called when an encapsulated packet arrives on an established session.
    void (*on_packet_receive)(void* impl, const char* ip, uint16_t port, const uint8_t* data,
                              size_t len);
    // Called when an UNCONNECTED_PING arrives (for status queries).
    void (*on_raw_packet)(void* impl, const char* ip, uint16_t port, const uint8_t* data,
                          size_t len);
    // Called periodically so the main thread can tick.
    void (*on_tick)(void* impl);
} mcp_server_instance_t;

