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
// UDPServerSocket.c — POSIX + Winsock2 implementation. Pure C.
#include "UDPServerSocket.h"

#include <string.h>
#include <errno.h>

#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  pragma comment(lib, "ws2_32.lib")
typedef int socklen_t;
typedef int ssize_t_sock; // not standard; use below
#  define CLOSE_SOCKET closesocket
#  define SOCK_ERRNO  WSAGetLastError()
#  define SOCK_AGAIN  WSAEWOULDBLOCK
static int winsock_initialized = 0;
static void ensure_winsock(void) {
    if (winsock_initialized) return;
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    winsock_initialized = 1;
}
#else
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <unistd.h>
#  include <fcntl.h>
#  define CLOSE_SOCKET close
#  define SOCK_ERRNO  errno
#  define SOCK_AGAIN  EAGAIN
static void ensure_winsock(void) {}
#endif

int mcp_udp_socket_open(mcp_udp_socket_t* s, const char* host, uint16_t port) {
    if (!s) return -1;
    ensure_winsock();
    s->fd = (int)socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s->fd < 0) return -1;

    // Allow address reuse.
    int yes = 1;
    setsockopt(s->fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof(yes));

    // Big kernel buffers, both ways. The default SO_RCVBUF (~200KB) is
    // easy to overrun when one player's chunk stream (dozens of 1400-byte
    // datagrams per tick, per player) lands in the same tick as the OTHER
    // player's keepalives: the kernel then DROPS the datagrams it has no
    // room for, and the player whose ping got dropped is the one who
    // "suddenly stopped answering" and gets reaped by the timeout sweep
    // even though their link was fine the whole time — which is exactly
    // the "the second player joins and then everybody drops" report.
    // Asking for 4MB costs nothing (it is not allocated up front) and
    // only matters when several players stream chunks at once.
    int bufSize = 4 * 1024 * 1024;
    setsockopt(s->fd, SOL_SOCKET, SO_RCVBUF, (const char*)&bufSize, sizeof(bufSize));
    setsockopt(s->fd, SOL_SOCKET, SO_SNDBUF, (const char*)&bufSize, sizeof(bufSize));
#ifdef SO_RCVBUFFORCE
    // Linux caps SO_RCVBUF at net.core.rmem_max unless the socket asks
    // for more than a privileged process may set; ignore the failure
    // (the value above is still applied) so this stays portable.
    setsockopt(s->fd, SOL_SOCKET, SO_RCVBUFFORCE, (const char*)&bufSize, sizeof(bufSize));
#endif

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (!host || host[0] == '\0' || strcmp(host, "0.0.0.0") == 0) {
        addr.sin_addr.s_addr = INADDR_ANY;
    } else {
        inet_pton(AF_INET, host, &addr.sin_addr);
    }
    if (bind(s->fd, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        CLOSE_SOCKET(s->fd);
        s->fd = -1;
        return -1;
    }
    s->blocking = true;
    return 0;
}

void mcp_udp_socket_close(mcp_udp_socket_t* s) {
    if (!s || s->fd < 0) return;
    CLOSE_SOCKET(s->fd);
    s->fd = -1;
}

void mcp_udp_socket_set_blocking(mcp_udp_socket_t* s, bool blocking) {
    if (!s || s->fd < 0) return;
#ifdef _WIN32
    u_long mode = blocking ? 0 : 1;
    ioctlsocket(s->fd, FIONBIO, &mode);
#else
    int flags = fcntl(s->fd, F_GETFL, 0);
    if (blocking) {
        fcntl(s->fd, F_SETFL, flags & ~O_NONBLOCK);
    } else {
        fcntl(s->fd, F_SETFL, flags | O_NONBLOCK);
    }
#endif
    s->blocking = blocking;
}

ssize_t mcp_udp_socket_recv(mcp_udp_socket_t* s,
                              void* buf, size_t cap,
                              uint8_t* src_ip, uint16_t* src_port) {
    if (!s || s->fd < 0 || !buf || cap == 0) return -1;
    struct sockaddr_in from;
    socklen_t fromLen = sizeof(from);
    ssize_t n = recvfrom(s->fd, (char*)buf, (int)cap, 0,
                         (struct sockaddr*)&from, &fromLen);
    if (n < 0) {
        if (SOCK_ERRNO == SOCK_AGAIN) return -EAGAIN;
        return -1;
    }
    if (src_ip) {
        memcpy(src_ip, &from.sin_addr.s_addr, 4);
    }
    if (src_port) {
        *src_port = ntohs(from.sin_port);
    }
    return n;
}

ssize_t mcp_udp_socket_send(mcp_udp_socket_t* s,
                              const void* buf, size_t len,
                              const uint8_t* dst_ip, uint16_t dst_port) {
    if (!s || s->fd < 0 || !buf || len == 0 || !dst_ip) return -1;
    struct sockaddr_in to;
    memset(&to, 0, sizeof(to));
    to.sin_family = AF_INET;
    to.sin_port = htons(dst_port);
    memcpy(&to.sin_addr.s_addr, dst_ip, 4);
    return sendto(s->fd, (const char*)buf, (int)len, 0,
                   (struct sockaddr*)&to, sizeof(to));
}
