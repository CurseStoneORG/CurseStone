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
// UDPServerSocket — cross-platform UDP socket abstraction. Pure C.
// Hides POSIX <sys/socket.h> vs Winsock2 behind a uniform interface.
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#if defined(_MSC_VER)
#  include <BaseTsd.h>
// MSVC does not provide ssize_t; map it to the signed pointer-sized SSIZE_T.
typedef SSIZE_T ssize_t;
#else
#  include <sys/types.h>   // ssize_t on POSIX and MinGW
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mcp_udp_socket {
    int       fd;     // POSIX int / Win32 SOCKET cast to int
    bool      blocking;
} mcp_udp_socket_t;

// Open a UDP socket bound to `host`:`port` (IPv4). Returns 0 on success.
int mcp_udp_socket_open(mcp_udp_socket_t* s, const char* host, uint16_t port);

// Close.
void mcp_udp_socket_close(mcp_udp_socket_t* s);

// Set non-blocking mode.
void mcp_udp_socket_set_blocking(mcp_udp_socket_t* s, bool blocking);

// Receive a datagram into `buf` (size `cap`). Returns the actual byte
// count, or -1 on error (or -EAGAIN when non-blocking and no data).
// On success, fills `*src_ip` (4 bytes) and `*src_port`.
ssize_t mcp_udp_socket_recv(mcp_udp_socket_t* s,
                              void* buf, size_t cap,
                              uint8_t* src_ip, uint16_t* src_port);

// Send a datagram to `dst_ip`:`dst_port`. Returns bytes sent, or -1.
ssize_t mcp_udp_socket_send(mcp_udp_socket_t* s,
                              const void* buf, size_t len,
                              const uint8_t* dst_ip, uint16_t dst_port);

#ifdef __cplusplus
} // extern "C"
#endif
