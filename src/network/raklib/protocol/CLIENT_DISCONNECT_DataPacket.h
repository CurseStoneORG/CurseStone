// SPDX-License-Identifier: LGPL-3.0-or-later
// AUTO-GENERATED. RakNet packet CLIENT_DISCONNECT_DataPacket. Pure C.
#pragma once
#include "Packet.h"
#include "../../../utils/BinaryStream.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mcp_pkt_client_disconnect {
    mcp_packet_t header;
    int64_t  client_id; int64_t server_id;
} mcp_pkt_client_disconnect_t;

mcp_packet_t* mcp_pkt_client_disconnect_new(void);

#ifdef __cplusplus
}
#endif
