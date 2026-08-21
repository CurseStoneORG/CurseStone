// SPDX-License-Identifier: LGPL-3.0-or-later
// AUTO-GENERATED. RakNet packet OPEN_CONNECTION_REPLY_2. Pure C.
#pragma once
#include "Packet.h"
#include "../../../utils/BinaryStream.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mcp_pkt_open_connection_reply_2 {
    mcp_packet_t header;
    
    uint8_t  magic[16];
    int64_t  server_id;
    uint8_t  client_ip[4];
    uint16_t client_port;
    uint16_t mtu;
    uint8_t  security_flag;

} mcp_pkt_open_connection_reply_2_t;

mcp_packet_t* mcp_pkt_open_connection_reply_2_new(void);

#ifdef __cplusplus
}
#endif
