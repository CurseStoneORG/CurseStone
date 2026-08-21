// SPDX-License-Identifier: LGPL-3.0-or-later
// AUTO-GENERATED. RakNet packet UNCONNECTED_PONG. Pure C.
#pragma once
#include "Packet.h"
#include "../../../utils/BinaryStream.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mcp_pkt_unconnected_pong {
    mcp_packet_t header;
    
    uint64_t server_id;
    uint8_t  magic[16];
    uint64_t time;
    uint8_t  server_id_str[8];
    char*    motd;
    size_t   motd_len;

} mcp_pkt_unconnected_pong_t;

mcp_packet_t* mcp_pkt_unconnected_pong_new(void);

#ifdef __cplusplus
}
#endif
