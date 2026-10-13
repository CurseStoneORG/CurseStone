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
// SERVER_HANDSHAKE_DataPacket (0x10) — sent encapsulated by server
// Layout: [id=0x10][clientAddr:7][u16=0][10 x sysAddr:7each][sendPing i64][sendPong i64]
// Total: 1 + 7 + 2 + 70 + 8 + 8 = 96 bytes
#pragma once
#include "Packet.h"
#include "../../../utils/BinaryStream.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mcp_pkt_server_handshake {
    mcp_packet_t header;
    uint8_t  client_ip[4];
    uint16_t client_port;
    int64_t  send_ping;
    int64_t  send_pong;
} mcp_pkt_server_handshake_t;

mcp_packet_t* mcp_pkt_server_handshake_new(void);

#ifdef __cplusplus
}
#endif
