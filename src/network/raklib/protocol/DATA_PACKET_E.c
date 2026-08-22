// SPDX-License-Identifier: LGPL-3.0-or-later
// AUTO-GENERATED. RakNet packet DATA_PACKET_E. Pure C.
#include "DATA_PACKET_E.h"
#include "../RakLib.h"
#include <stdlib.h>
#include <string.h>

static uint8_t pkt_id(void) { return 0x8e; }
static void pkt_encode(void* self, mcp_bstream_t* out) {
    mcp_pkt_data_packet_e_t* p = (mcp_pkt_data_packet_e_t*)self;
    mcp_bstream_write_u8(out, 0x8e);
    (void)p;
}
static void pkt_decode(void* self, mcp_bstream_t* in) {
    mcp_pkt_data_packet_e_t* p = (mcp_pkt_data_packet_e_t*)self;
    (void)p;
    uint8_t id;
    mcp_bstream_read_u8(in, &id);
    (void)p;
}
static size_t pkt_size(const void* self) {
    (void)self;
    return 512;
}
static void pkt_destroy(void* self) {
    if (!self) return;
    free(self);
}
static const mcp_packet_vtable_t VT = {
    pkt_id, pkt_encode, pkt_decode, pkt_size, pkt_destroy
};

mcp_packet_t* mcp_pkt_data_packet_e_new(void) {
    mcp_pkt_data_packet_e_t* p = (mcp_pkt_data_packet_e_t*)calloc(1, sizeof(*p));
    if (!p) return NULL;
    p->header.vtable = &VT;
    return (mcp_packet_t*)p;
}
