// SPDX-License-Identifier: LGPL-3.0-or-later
// Packet — base RakNet packet interface. Pure C vtable.
#pragma once

#include "../../../utils/BinaryStream.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mcp_packet_vtable {
    uint8_t  (*id)(void);
    void     (*encode)(void* self, mcp_bstream_t* out);
    void     (*decode)(void* self, mcp_bstream_t* in);
    size_t   (*size_estimate)(const void* self);
    void     (*destroy)(void* self);
} mcp_packet_vtable_t;

typedef struct mcp_packet {
    const mcp_packet_vtable_t* vtable;
} mcp_packet_t;

#ifdef __cplusplus
} // extern "C"
#endif
