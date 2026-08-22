// SPDX-License-Identifier: LGPL-3.0-or-later
// AUTO-GENERATED. RakNet packet CLIENT_CONNECT_DataPacket. Pure C.
#pragma once
#include "Packet.h"
#include "../../../utils/BinaryStream.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mcp_pkt_client_connect {
    mcp_packet_t header;
    
    int64_t  client_id;
    uint64_t  connect_time;
    uint8_t   client_ip[4];
    uint16_t  client_port;
    int64_t   client_timestamp;
    int64_t   server_id;        // set in SERVER_HANDSHAKE

} mcp_pkt_client_connect_t;

mcp_packet_t* mcp_pkt_client_connect_new(void);

#ifdef __cplusplus
}
#endif
