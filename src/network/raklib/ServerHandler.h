// SPDX-License-Identifier: LGPL-3.0-or-later
// ServerHandler — IPC bridge: lets the main thread send commands to the
// RakLib worker (kickSession, send encapsulated packet, set blocking, etc.)
// Pure C.
#pragma once

#include "SessionManager.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mcp_server_handler {
    mcp_rak_session_manager_t* manager;  // not owned
} mcp_server_handler_t;

void mcp_server_handler_init(mcp_server_handler_t* h,
                                mcp_rak_session_manager_t* m);
void mcp_server_handler_destroy(mcp_server_handler_t* h);

// Send a raw datagram (already-encoded) to a remote client.
bool mcp_server_handler_send_raw(mcp_server_handler_t* h,
                                   const uint8_t ip[4], uint16_t port,
                                   const uint8_t* data, size_t len);

// Send an encapsulated packet to an established session. `payload` is
// wrapped into a DATA_PACKET_0 datagram before sending.
bool mcp_server_handler_send_encapsulated(mcp_server_handler_t* h,
                                            const uint8_t ip[4], uint16_t port,
                                            const uint8_t* payload, size_t len,
                                            uint8_t reliability);

// Close a session immediately (no goodbye packet).
void mcp_server_handler_close_session(mcp_server_handler_t* h,
                                        const uint8_t ip[4], uint16_t port);

#ifdef __cplusplus
} // extern "C"
#endif
