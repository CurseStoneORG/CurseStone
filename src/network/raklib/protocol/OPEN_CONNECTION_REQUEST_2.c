// SPDX-License-Identifier: LGPL-3.0-or-later
// OPEN_CONNECTION_REQUEST_2 (0x07) implementation
#include "OPEN_CONNECTION_REQUEST_2.h"
#include "../RakLib.h"
#include <stdlib.h>
#include <string.h>

static uint8_t pkt_id(void) { return 0x07; }

static void pkt_decode(void* self, mcp_bstream_t* in) {
    mcp_pkt_open_connection_request_2_t* p = (mcp_pkt_open_connection_request_2_t*)self;
    uint8_t id; mcp_bstream_read_u8(in, &id);
    mcp_bstream_read_bytes(in, p->magic, 16);
    // server address: [ver u8][ip 4][port u16 BE]
    uint8_t ver; mcp_bstream_read_u8(in, &ver);
    mcp_bstream_read_bytes(in, p->server_ip, 4);
    mcp_bstream_read_u16_be(in, &p->server_port);
    // mtu
    mcp_bstream_read_u16_be(in, &p->mtu);
    if (p->mtu > 1492) p->mtu = 1492;
    // clientID
    uint64_t v; mcp_bstream_read_u64_be(in, &v); p->client_id = (int64_t)v;
}

static void pkt_encode(void* self, mcp_bstream_t* out) {
    mcp_pkt_open_connection_request_2_t* p = (mcp_pkt_open_connection_request_2_t*)self;
    mcp_bstream_write_u8(out, 0x07);
    mcp_bstream_write_bytes(out, p->magic, 16);
    mcp_bstream_write_u8(out, 4); // IPv4
    mcp_bstream_write_bytes(out, p->server_ip, 4);
    mcp_bstream_write_u16_be(out, p->server_port);
    mcp_bstream_write_u16_be(out, p->mtu);
    mcp_bstream_write_u64_be(out, (uint64_t)p->client_id);
}

static size_t pkt_size(const void* self) { (void)self; return 34; }

static void pkt_destroy(void* self) { if (self) free(self); }

static const mcp_packet_vtable_t VT = {
    pkt_id, pkt_encode, pkt_decode, pkt_size, pkt_destroy
};

mcp_packet_t* mcp_pkt_open_connection_request_2_new(void) {
    mcp_pkt_open_connection_request_2_t* p =
        (mcp_pkt_open_connection_request_2_t*)calloc(1, sizeof(*p));
    if (!p) return NULL;
    p->header.vtable = &VT;
    return (mcp_packet_t*)p;
}
