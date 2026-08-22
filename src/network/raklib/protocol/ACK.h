// SPDX-License-Identifier: LGPL-3.0-or-later
// AUTO-GENERATED. RakNet packet ACK. Pure C.
#pragma once
#include "Packet.h"
#include "../../../utils/BinaryStream.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mcp_pkt_ack {
    mcp_packet_t header;
    
    bool    is_nack;            // false for ACK (0xc0), true for NACK (0xa0)
    uint32_t* records_min;
    uint32_t* records_max;
    size_t   record_count;

} mcp_pkt_ack_t;

mcp_packet_t* mcp_pkt_ack_new(void);

#ifdef __cplusplus
}
#endif
