// SPDX-License-Identifier: LGPL-3.0-or-later
// AUTO-GENERATED. RakNet packet PONG_DataPacket. Pure C.
#pragma once
#include "Packet.h"
#include "../../../utils/BinaryStream.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mcp_pkt_pong {
    mcp_packet_t header;
    int64_t ping_time; int64_t pong_time;
} mcp_pkt_pong_t;

mcp_packet_t* mcp_pkt_pong_new(void);

#ifdef __cplusplus
}
#endif
