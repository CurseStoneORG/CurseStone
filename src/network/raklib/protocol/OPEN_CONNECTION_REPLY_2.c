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
// AUTO-GENERATED. RakNet packet OPEN_CONNECTION_REPLY_2. Pure C.
#include "OPEN_CONNECTION_REPLY_2.h"
#include "../RakLib.h"
#include <stdlib.h>
#include <string.h>

static uint8_t pkt_id(void) {
    return 0x08;
}
static void pkt_encode(void* self, mcp_bstream_t* out) {
    mcp_pkt_open_connection_reply_2_t* p = (mcp_pkt_open_connection_reply_2_t*)self;
    mcp_bstream_write_u8(out, 0x08);
    mcp_bstream_write_bytes(out, p->magic, 16);
    mcp_bstream_write_u64_be(out, (uint64_t)p->server_id);
    mcp_bstream_write_bytes(out, p->client_ip, 4);
    mcp_bstream_write_u16_be(out, p->client_port);
    mcp_bstream_write_u16_be(out, p->mtu);
    mcp_bstream_write_u8(out, p->security_flag);
}
static void pkt_decode(void* self, mcp_bstream_t* in) {
    mcp_pkt_open_connection_reply_2_t* p = (mcp_pkt_open_connection_reply_2_t*)self;
    (void)p;
    uint8_t id;
    mcp_bstream_read_u8(in, &id);
    mcp_bstream_read_bytes(in, p->magic, 16);
    {
        uint64_t v;
        mcp_bstream_read_u64_be(in, &v);
        p->server_id = (int64_t)v;
    }
    mcp_bstream_read_bytes(in, p->client_ip, 4);
    mcp_bstream_read_u16_be(in, &p->client_port);
    mcp_bstream_read_u16_be(in, &p->mtu);
    mcp_bstream_read_u8(in, &p->security_flag);
}
static size_t pkt_size(const void* self) {
    (void)self;
    return 32;
}
static void pkt_destroy(void* self) {
    if (!self)
        return;
    free(self);
}
static const mcp_packet_vtable_t VT = {pkt_id, pkt_encode, pkt_decode, pkt_size, pkt_destroy};

mcp_packet_t* mcp_pkt_open_connection_reply_2_new(void) {
    mcp_pkt_open_connection_reply_2_t* p =
        (mcp_pkt_open_connection_reply_2_t*)calloc(1, sizeof(*p));
    if (!p)
        return NULL;
    p->header.vtable = &VT;
    return (mcp_packet_t*)p;
}
