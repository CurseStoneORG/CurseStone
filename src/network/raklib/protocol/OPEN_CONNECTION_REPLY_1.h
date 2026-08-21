// SPDX-License-Identifier: LGPL-3.0-or-later
// AUTO-GENERATED. RakNet packet OPEN_CONNECTION_REPLY_1. Pure C.
#pragma once
#include "Packet.h"
#include "../../../utils/BinaryStream.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mcp_pkt_open_connection_reply_1 {
    mcp_packet_t header;
    
    uint8_t  magic[16];
    int64_t  server_id;
    uint8_t  security_flag;     // 0 = no encryption
    uint16_t mtu;

} mcp_pkt_open_connection_reply_1_t;

mcp_packet_t* mcp_pkt_open_connection_reply_1_new(void);

#ifdef __cplusplus
}
#endif
