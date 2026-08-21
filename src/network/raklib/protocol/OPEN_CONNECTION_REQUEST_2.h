// SPDX-License-Identifier: LGPL-3.0-or-later
// OPEN_CONNECTION_REQUEST_2 (0x07) — sent by MCPE client
// Layout: [id][magic 16][serverAddress: ver+ip4+port][mtu u16 BE][clientID i64]
#pragma once
#include "Packet.h"
#include "../../../utils/BinaryStream.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mcp_pkt_open_connection_request_2 {
    mcp_packet_t header;
    uint8_t  magic[16];
    uint8_t  server_ip[4];   // server address IP
    uint16_t server_port;    // server address port
    uint16_t mtu;
    int64_t  client_id;
} mcp_pkt_open_connection_request_2_t;

mcp_packet_t* mcp_pkt_open_connection_request_2_new(void);

#ifdef __cplusplus
}
#endif
