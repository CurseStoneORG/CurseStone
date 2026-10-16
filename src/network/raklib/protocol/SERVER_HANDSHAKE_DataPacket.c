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
// SERVER_HANDSHAKE_DataPacket (0x10)
// Ported from raklib/protocol/SERVER_HANDSHAKE_DataPacket
#include "SERVER_HANDSHAKE_DataPacket.h"
#include "../RakLib.h"
#include <stdlib.h>
#include <string.h>

static const uint8_t NULL_IP[4] = {0, 0, 0, 0};
static const uint8_t LOOPBACK[4] = {127, 0, 0, 1};

static uint8_t pkt_id(void) {
    return 0x10;
}

static void write_addr(mcp_bstream_t* out, const uint8_t ip[4], uint16_t port) {
    mcp_bstream_write_u8(out, 4); // IPv4
    mcp_bstream_write_bytes(out, ip, 4);
    mcp_bstream_write_u16_be(out, port);
}

static void pkt_encode(void* self, mcp_bstream_t* out) {
    mcp_pkt_server_handshake_t* p = (mcp_pkt_server_handshake_t*)self;
    mcp_bstream_write_u8(out, 0x10);
    // client address
    write_addr(out, p->client_ip, p->client_port);
    // unknown short = 0
    mcp_bstream_write_u16_be(out, 0);
    // 10 system addresses: first is 127.0.0.1:0, rest are 0.0.0.0:0
    write_addr(out, LOOPBACK, 0);
    for (int i = 1; i < 10; ++i)
        write_addr(out, NULL_IP, 0);
    // sendPing + sendPong
    mcp_bstream_write_u64_be(out, (uint64_t)p->send_ping);
    mcp_bstream_write_u64_be(out, (uint64_t)p->send_pong);
}

static void pkt_decode(void* self, mcp_bstream_t* in) {
    // Not needed server-side
    (void)self;
    (void)in;
}

static size_t pkt_size(const void* self) {
    (void)self;
    return 96; // 1+7+2+70+8+8
}

static void pkt_destroy(void* self) {
    if (self)
        free(self);
}

static const mcp_packet_vtable_t VT = {pkt_id, pkt_encode, pkt_decode, pkt_size, pkt_destroy};

mcp_packet_t* mcp_pkt_server_handshake_new(void) {
    mcp_pkt_server_handshake_t* p = (mcp_pkt_server_handshake_t*)calloc(1, sizeof(*p));
    if (!p)
        return NULL;
    p->header.vtable = &VT;
    return (mcp_packet_t*)p;
}
