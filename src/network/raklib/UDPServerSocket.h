// SPDX-License-Identifier: LGPL-3.0-or-later
// UDPServerSocket — cross-platform UDP socket abstraction. Pure C.
// Hides POSIX <sys/socket.h> vs Winsock2 behind a uniform interface.
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <sys/types.h>   // for ssize_t on POSIX

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
