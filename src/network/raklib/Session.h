// SPDX-License-Identifier: LGPL-3.0-or-later
// Session.h — per-client RakNet state machine. Pure C.
// Compatible with MCPE 0.15.10 / protocol 84 / RakLib 0.8.x
#pragma once

#include "UDPServerSocket.h"
#include "protocol/PacketReliability.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---- Session states (mirrors PMMP Session.php) ----
#define MCP_SESSION_STATE_UNCONNECTED  0
#define MCP_SESSION_STATE_CONNECTING_1 1   // after OCR1 → waiting OCR2
#define MCP_SESSION_STATE_CONNECTING_2 2   // after OCR2 → waiting CLIENT_CONNECT (0x09)
#define MCP_SESSION_STATE_CONNECTED    3   // fully established

// Maximum encapsulated packet size inside one DataPacket datagram (per PMMP).
#define MCP_SESSION_MAX_SPLIT_SIZE   128
#define MCP_SESSION_MAX_SPLIT_COUNT    4
#define MCP_SESSION_WINDOW_SIZE     2048

// RakNet encapsulated packet: the header inside a Data datagram.
// Simplified: only tracks fields needed for Proto 84 / MCPE 0.15.10.
typedef struct mcp_encapsulated_header {
    uint8_t  reliability;       // PacketReliability value
    bool     has_split;
    uint32_t message_index;     // present if RELIABLE
    uint32_t order_index;       // present if ORDERED
    uint8_t  order_channel;
    uint16_t split_id;
    uint32_t split_index;
    uint32_t split_count;
} mcp_encap_header_t;

// A segment of the split-packet reassembly buffer.
#define MCP_MAX_SPLIT_SEGMENTS 256
typedef struct mcp_split_store {
    uint16_t split_id;
    uint32_t count;
    uint32_t received;
    uint8_t* segments[MCP_MAX_SPLIT_SEGMENTS]; // heap-allocated
    size_t   seg_sizes[MCP_MAX_SPLIT_SEGMENTS];
    bool     in_use;
} mcp_split_store_t;

#define MCP_MAX_ACK_QUEUE    256
#define MCP_MAX_NACK_QUEUE   256
#define MCP_RECOVERY_CAP     512

// Per-session send queue slot.
typedef struct mcp_send_slot {
    uint8_t* data;
    size_t   len;
    uint64_t send_time_ms;
    uint32_t seq;
} mcp_send_slot_t;

typedef struct mcp_rak_session {
    // Identity
    uint8_t   ip[4];
    uint16_t  port;
    int64_t   client_id;
    int64_t   server_id;

    // State machine
    uint8_t   state;            // MCP_SESSION_STATE_*

    // Sequence numbers
    uint32_t  send_seq;
    int32_t   last_recv_seq;    // -1 = nothing received yet

    // Sliding window
    uint32_t  window_start;
    uint32_t  window_end;
    bool      recv_window[MCP_SESSION_WINDOW_SIZE];

    // ACK / NACK queues (sequence numbers pending transmission)
    uint32_t  ack_queue[MCP_MAX_ACK_QUEUE];
    size_t    ack_queue_len;
    uint32_t  nack_queue[MCP_MAX_NACK_QUEUE];
    size_t    nack_queue_len;

    // Send queue (DataPacket 0x84 datagrams awaiting flush)
    uint8_t** send_queue_bufs;
    size_t*   send_queue_lens;
    size_t    send_queue_count;
    size_t    send_queue_cap;

    // Recovery queue (for retransmit): indexed by seq % MCP_RECOVERY_CAP
    mcp_send_slot_t recovery[MCP_RECOVERY_CAP];

    // Reliability counters
    uint32_t  message_index;    // monotonically increasing per RELIABLE pkt
    uint32_t  channel_index[32];// per ordered-channel index

    // Split packet reassembly (one active store per session is enough for MCPE)
    mcp_split_store_t split;

    // MTU negotiated during handshake
    uint16_t  mtu;

    // Timing
    uint64_t  last_activity_ms;
    uint64_t  start_time_ms;
    bool      is_active;        // received at least one packet this tick

    // splitID counter for outbound fragmented packets
    uint16_t  split_id_counter;

    // Bug 2 fix: set when CLIENT_DISCONNECT (0x15) is received.
    // The session is cleaned up on the next tick sweep — no double-close.
    bool      is_disconnecting;
} mcp_rak_session_t;

// Init / destroy
void mcp_rak_session_init(mcp_rak_session_t* s, const uint8_t ip[4],
                           uint16_t port, int64_t client_id, int64_t server_id);
void mcp_rak_session_destroy(mcp_rak_session_t* s);

// ACK handling
void   mcp_rak_session_queue_ack(mcp_rak_session_t* s, uint32_t seq);
void   mcp_rak_session_queue_nack(mcp_rak_session_t* s, uint32_t seq);
size_t mcp_rak_session_drain_ack_queue(mcp_rak_session_t* s,
                                        uint32_t* out_mins, uint32_t* out_maxs,
                                        size_t out_cap);
size_t mcp_rak_session_drain_nack_queue(mcp_rak_session_t* s,
                                         uint32_t* out_mins, uint32_t* out_maxs,
                                         size_t out_cap);

// Encapsulation: wrap a MCPE payload inside a RakNet encapsulated record
// inside a DataPacket_4 (0x84) datagram. Returns number of bytes written.
size_t mcp_rak_session_build_datagram(mcp_rak_session_t* s,
                                       uint8_t* out, size_t out_cap,
                                       const uint8_t* payload, size_t plen,
                                       uint8_t reliability, uint8_t order_channel);

// Enqueue a datagram for sending; flushed by tick.
bool mcp_rak_session_enqueue(mcp_rak_session_t* s,
                              const uint8_t* data, size_t len);

// Timeout check
bool mcp_rak_session_is_timed_out(const mcp_rak_session_t* s,
                                    uint64_t now_ms, uint64_t timeout_ms);

#ifdef __cplusplus
} // extern "C"
#endif
