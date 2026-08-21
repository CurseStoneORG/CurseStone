// SPDX-License-Identifier: LGPL-3.0-or-later
// AUTO-GENERATED. RakNet packet OPEN_CONNECTION_REQUEST_1. Pure C.
#pragma once
#include "Packet.h"
#include "../../../utils/BinaryStream.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mcp_pkt_open_connection_request_1 {
    mcp_packet_t header;
    
    uint8_t  magic[16];
    uint8_t  protocol;
    uint16_t mtu;

} mcp_pkt_open_connection_request_1_t;

mcp_packet_t* mcp_pkt_open_connection_request_1_new(void);

#ifdef __cplusplus
}
#endif
