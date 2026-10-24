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
// ServerHandler.c — thin IPC bridge. Pure C.
#include "ServerHandler.h"

#include <stdlib.h>
#include <string.h>

void mcp_server_handler_init(mcp_server_handler_t* h, mcp_rak_session_manager_t* m) {
    if (!h)
        return;
    h->manager = m;
}
void mcp_server_handler_destroy(mcp_server_handler_t* h) {
    if (h)
        h->manager = NULL;
}

bool mcp_server_handler_send_raw(mcp_server_handler_t* h, const uint8_t ip[4], uint16_t port,
                                 const uint8_t* data, size_t len) {
    if (!h || !h->manager || !data)
        return false;
    ssize_t n = mcp_rak_session_manager_send(h->manager, data, len, ip, port);
    return n > 0 && (size_t)n == len;
}

bool mcp_server_handler_send_encapsulated(mcp_server_handler_t* h, const uint8_t ip[4],
                                          uint16_t port, const uint8_t* payload, size_t len,
                                          uint8_t reliability) {
    if (!h || !h->manager || !payload || len == 0)
        return false;
    // Look up the session (it must already exist).
    mcp_rak_session_t* s = NULL;
    for (size_t i = 0; i < h->manager->session_count; ++i) {
        mcp_rak_session_t* cur = h->manager->sessions[i];
        if (memcmp(cur->ip, ip, 4) == 0 && cur->port == port) {
            s = cur;
            break;
        }
    }
    if (!s)
        return false;
    // Use send_payload so large packets (e.g. chunk data ~15KB) are split
    // into MTU-sized fragments automatically. Without splitting, a single
    // 15KB UDP datagram exceeds mobile MTU (576 bytes) and is lost, causing
    // the client to time out with "Disconnected from server".
    return mcp_rak_session_send_payload(s, h->manager, payload, len, reliability,
                                        /*order_channel=*/0);
}

void mcp_server_handler_close_session(mcp_server_handler_t* h, const uint8_t ip[4], uint16_t port) {
    if (!h || !h->manager)
        return;
    mcp_rak_session_manager_t* m = h->manager;
    for (size_t i = 0; i < m->session_count; ++i) {
        if (memcmp(m->sessions[i]->ip, ip, 4) == 0 && m->sessions[i]->port == port) {
            // Release the session-owned buffers (recovery, split segments,
            // order holds) before freeing the struct, like every other
            // teardown path does.
            mcp_rak_session_destroy(m->sessions[i]);
            free(m->sessions[i]);
            m->sessions[i] = m->sessions[--m->session_count];
            return;
        }
    }
}
