// SPDX-License-Identifier: LGPL-3.0-or-later
// AUTO-GENERATED. RakNet packet PONG_DataPacket. Pure C.
#include "PONG_DataPacket.h"
#include "../RakLib.h"
#include <stdlib.h>
#include <string.h>

static uint8_t pkt_id(void) { return 0x03; }
static void pkt_encode(void* self, mcp_bstream_t* out) {
    mcp_pkt_pong_t* p = (mcp_pkt_pong_t*)self;
    mcp_bstream_write_u8(out, 0x03);
    mcp_bstream_write_u64_be(out, (uint64_t)p->ping_time); mcp_bstream_write_u64_be(out, (uint64_t)p->pong_time);
}
static void pkt_decode(void* self, mcp_bstream_t* in) {
    mcp_pkt_pong_t* p = (mcp_pkt_pong_t*)self;
    (void)p;
    uint8_t id;
    mcp_bstream_read_u8(in, &id);
    mcp_bstream_read_u64_be(in, (uint64_t*)&p->ping_time); mcp_bstream_read_u64_be(in, (uint64_t*)&p->pong_time);
}
static size_t pkt_size(const void* self) {
    (void)self;
    return 17;
}
static void pkt_destroy(void* self) {
    if (!self) return;
    free(self);
}
static const mcp_packet_vtable_t VT = {
    pkt_id, pkt_encode, pkt_decode, pkt_size, pkt_destroy
};

mcp_packet_t* mcp_pkt_pong_new(void) {
    mcp_pkt_pong_t* p = (mcp_pkt_pong_t*)calloc(1, sizeof(*p));
    if (!p) return NULL;
    p->header.vtable = &VT;
    return (mcp_packet_t*)p;
}
