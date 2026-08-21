// SPDX-License-Identifier: LGPL-3.0-or-later
// AUTO-GENERATED. RakNet packet UNCONNECTED_PING. Pure C.
#pragma once
#include "Packet.h"
#include "../../../utils/BinaryStream.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mcp_pkt_unconnected_ping {
    mcp_packet_t header;
    
    uint64_t time;
    uint8_t  magic[16];
    int64_t  client_id;

} mcp_pkt_unconnected_ping_t;

mcp_packet_t* mcp_pkt_unconnected_ping_new(void);

#ifdef __cplusplus
}
#endif
