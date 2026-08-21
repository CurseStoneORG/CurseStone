// SPDX-License-Identifier: LGPL-3.0-or-later
// PacketReliability — RakNet encapsulated packet reliability levels.
// Values match raklib/protocol/PacketReliability.php (PMMP)
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Reliability constants (3 bits in encapsulated header flags >> 5)
#define MCP_RELIABILITY_UNRELIABLE                      0
#define MCP_RELIABILITY_UNRELIABLE_SEQUENCED            1
#define MCP_RELIABILITY_RELIABLE                        2
#define MCP_RELIABILITY_RELIABLE_ORDERED                3
#define MCP_RELIABILITY_RELIABLE_SEQUENCED              4
#define MCP_RELIABILITY_RELIABLE_WITH_ACK_RECEIPT       6
#define MCP_RELIABILITY_RELIABLE_ORDERED_WITH_ACK_RECEIPT 7

// Keep the old typedef for compatibility
typedef uint8_t mcp_reliability_t;

#ifdef __cplusplus
} // extern "C"
#endif
