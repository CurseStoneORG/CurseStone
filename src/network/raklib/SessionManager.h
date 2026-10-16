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
// SessionManager.h — RakNet session manager, Protocol 84 / MCPE 0.15.10
// Ported from raklib/server/SessionManager
#pragma once

#include "Session.h"
#include "ServerInstance.h"
#include "UDPServerSocket.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MCP_SM_PACKET_LIMIT 250 // max packets per IP per tick (anti-flood)
#define MCP_SM_PORT_CHECKING true

typedef struct mcp_rak_session_manager {
    mcp_udp_socket_t socket;
    mcp_server_instance_t* instance; // not owned
    mcp_rak_session_t** sessions;
    size_t session_count;
    size_t session_capacity;
    int64_t server_id;
    uint16_t mtu;
    uint16_t bind_port; // remembered for CLIENT_HANDSHAKE check

    // MOTD / server name (UNCONNECTED_PONG)
    char server_name[256];

    // Anti-flood counters (per IP, cleared each tick)
    // Simple implementation: just count total packets from ip
    uint8_t ip_sec_buf[64][4]; // last 64 IPs seen this tick
    uint32_t ip_sec_cnt[64];
    size_t ip_sec_n;
} mcp_rak_session_manager_t;

void mcp_rak_session_manager_init(mcp_rak_session_manager_t* sm, int64_t server_id, uint16_t mtu,
                                  mcp_server_instance_t* instance);
void mcp_rak_session_manager_destroy(mcp_rak_session_manager_t* sm);

int mcp_rak_session_manager_bind(mcp_rak_session_manager_t* sm, const char* host, uint16_t port);

void mcp_rak_session_manager_tick(mcp_rak_session_manager_t* sm);

// Set the MOTD string (embedded in UNCONNECTED_PONG).
void mcp_rak_session_manager_set_name(mcp_rak_session_manager_t* sm, const char* name);

mcp_rak_session_t* mcp_rak_session_manager_get_or_create(mcp_rak_session_manager_t* sm,
                                                         const uint8_t ip[4], uint16_t port);

mcp_rak_session_t* mcp_rak_session_manager_find(mcp_rak_session_manager_t* sm, const uint8_t ip[4],
                                                uint16_t port);

ssize_t mcp_rak_session_manager_send(mcp_rak_session_manager_t* sm, const uint8_t* data, size_t len,
                                     const uint8_t ip[4], uint16_t port);

// Send an encapsulated MCPE packet through an established session.
// order_channel is only meaningful when reliability is one of the
// *_ORDERED variants; RakNet supports up to 32 independent ordering
// channels (0-31) so ordered traffic on one channel never blocks
// packets sent on a different channel.
bool mcp_rak_session_manager_send_encapsulated(mcp_rak_session_manager_t* sm, const uint8_t ip[4],
                                               uint16_t port, const uint8_t* payload, size_t plen,
                                               uint8_t reliability, uint8_t order_channel);

#ifdef __cplusplus
} // extern "C"
#endif
