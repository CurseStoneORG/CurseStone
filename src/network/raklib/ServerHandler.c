// SPDX-License-Identifier: LGPL-3.0-or-later
// ServerHandler.c — thin IPC bridge. Pure C.
#include "ServerHandler.h"

#include <stdlib.h>
#include <string.h>

void mcp_server_handler_init(mcp_server_handler_t* h,
                                mcp_rak_session_manager_t* m) {
    if (!h) return;
    h->manager = m;
}
void mcp_server_handler_destroy(mcp_server_handler_t* h) {
    if (h) h->manager = NULL;
}

bool mcp_server_handler_send_raw(mcp_server_handler_t* h,
                                   const uint8_t ip[4], uint16_t port,
                                   const uint8_t* data, size_t len) {
    if (!h || !h->manager || !data) return false;
    ssize_t n = mcp_rak_session_manager_send(h->manager, data, len, ip, port);
    return n > 0 && (size_t)n == len;
}

bool mcp_server_handler_send_encapsulated(mcp_server_handler_t* h,
                                            const uint8_t ip[4], uint16_t port,
                                            const uint8_t* payload, size_t len,
                                            uint8_t reliability) {
    if (!h || !h->manager || !payload || len == 0) return false;
    // Look up the session (it must already exist).
    mcp_rak_session_t* s = NULL;
    for (size_t i = 0; i < h->manager->session_count; ++i) {
        mcp_rak_session_t* cur = h->manager->sessions[i];
        if (memcmp(cur->ip, ip, 4) == 0 && cur->port == port) {
            s = cur;
            break;
        }
    }
    if (!s) return false;
    uint8_t buf[1500];
    // mcp_rak_session_build_datagram requires reliability + order_channel.
    // RELIABLE_ORDERED = 3 (matches PMMP defaults). Channel 0.
    size_t n = mcp_rak_session_build_datagram(s, buf, sizeof(buf),
                                                 payload, len,
                                                 reliability, /*order_channel=*/0);
    if (n == 0) return false;
    return mcp_server_handler_send_raw(h, ip, port, buf, n);
}

void mcp_server_handler_close_session(mcp_server_handler_t* h,
                                        const uint8_t ip[4], uint16_t port) {
    if (!h || !h->manager) return;
    mcp_rak_session_manager_t* m = h->manager;
    for (size_t i = 0; i < m->session_count; ++i) {
        if (memcmp(m->sessions[i]->ip, ip, 4) == 0 &&
            m->sessions[i]->port == port) {
            free(m->sessions[i]);
            m->sessions[i] = m->sessions[--m->session_count];
            return;
        }
    }
}
