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
// OPEN_CONNECTION_REQUEST_2 (0x07) — sent by MCPE client
// Layout: [id][magic 16][serverAddress: ver+ip4+port][mtu u16 BE][clientID i64]
#pragma once
#include "Packet.h"
#include "../../../utils/BinaryStream.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mcp_pkt_open_connection_request_2 {
    mcp_packet_t header;
    uint8_t  magic[16];
    uint8_t  server_ip[4];   // server address IP
    uint16_t server_port;    // server address port
    uint16_t mtu;
    int64_t  client_id;
} mcp_pkt_open_connection_request_2_t;

mcp_packet_t* mcp_pkt_open_connection_request_2_new(void);

#ifdef __cplusplus
}
#endif
