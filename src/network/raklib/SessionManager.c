// SPDX-License-Identifier: LGPL-3.0-or-later
// SessionManager.c — Full RakNet handshake + dispatch loop. Pure C.
// Ported from raklib/server/Session.php + SessionManager.php
// (PocketMine-MP 0.15.x / Protocol 84 compatible)
//
// Handshake flow implemented:
//   CLIENT                         SERVER
//   ──────                         ──────
//   UNCONNECTED_PING   (0x01/0x02) ──►  UNCONNECTED_PONG   (0x1c)
//   OCR1               (0x05)      ──►  OCR_REPLY_1        (0x06)   → STATE_CONNECTING_1
//   OCR2               (0x07)      ──►  OCR_REPLY_2        (0x08)   → STATE_CONNECTING_2
//   CLIENT_CONNECT     (0x09) enc  ──►  SERVER_HANDSHAKE   (0x10)   (unreliable encap)
//   CLIENT_HANDSHAKE   (0x13) enc  ──►  (open session)              → STATE_CONNECTED
//   [game packets]
//
#include "SessionManager.h"
#include "RakLib.h"
#include "../../utils/Utils.h"
#include "../../utils/BinaryStream.h"
#include <stdio.h>

// RakNet protocol packets
#include "protocol/Packet.h"
#include "protocol/UNCONNECTED_PING.h"
#include "protocol/UNCONNECTED_PING_OPEN_CONNECTIONS.h"
#include "protocol/UNCONNECTED_PONG.h"
#include "protocol/OPEN_CONNECTION_REQUEST_1.h"
#include "protocol/OPEN_CONNECTION_REPLY_1.h"
#include "protocol/OPEN_CONNECTION_REQUEST_2.h"
#include "protocol/OPEN_CONNECTION_REPLY_2.h"
#include "protocol/CLIENT_CONNECT_DataPacket.h"
#include "protocol/SERVER_HANDSHAKE_DataPacket.h"
#include "protocol/CLIENT_HANDSHAKE_DataPacket.h"
#include "protocol/CLIENT_DISCONNECT_DataPacket.h"
#include "protocol/PING_DataPacket.h"
#include "protocol/PONG_DataPacket.h"
#include "protocol/ACK.h"
#include "protocol/NACK.h"
#include "protocol/PacketReliability.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// RakNet magic bytes (offline message data id)
static const uint8_t RAKLIB_MAGIC_BYTES[16] = {
    0x00, 0xff, 0xff, 0x00, 0xfe, 0xfe, 0xfe, 0xfe,
    0xfd, 0xfd, 0xfd, 0xfd, 0x12, 0x34, 0x56, 0x78
};

// =========================================================================
// Init / Destroy
// =========================================================================
void mcp_rak_session_manager_init(mcp_rak_session_manager_t* sm,
                                    int64_t server_id, uint16_t mtu,
                                    mcp_server_instance_t* instance) {
    if (!sm) return;
    memset(sm, 0, sizeof(*sm));
    sm->socket.fd = -1;
    sm->server_id = server_id;
    sm->mtu       = mtu ? mtu : RAKLIB_MAX_MTU;
    sm->instance  = instance;
    snprintf(sm->server_name, sizeof(sm->server_name),
             "MCCPP;Demo;84;0.15.10;0;20");
}

void mcp_rak_session_manager_destroy(mcp_rak_session_manager_t* sm) {
    if (!sm) return;
    mcp_udp_socket_close(&sm->socket);
    if (sm->sessions) {
        for (size_t i = 0; i < sm->session_count; ++i) {
            mcp_rak_session_destroy(sm->sessions[i]);
            free(sm->sessions[i]);
        }
        free(sm->sessions);
    }
    memset(sm, 0, sizeof(*sm));
}

void mcp_rak_session_manager_set_name(mcp_rak_session_manager_t* sm,
                                       const char* name) {
    if (!sm || !name) return;
    snprintf(sm->server_name, sizeof(sm->server_name), "%s", name);
}

// =========================================================================
// Bind
// =========================================================================
int mcp_rak_session_manager_bind(mcp_rak_session_manager_t* sm,
                                   const char* host, uint16_t port) {
    if (!sm) return -1;
    int r = mcp_udp_socket_open(&sm->socket, host ? host : "0.0.0.0", port);
    if (r == 0) {
        mcp_udp_socket_set_blocking(&sm->socket, false);
        sm->bind_port = port;
    }
    return r;
}

// =========================================================================
// Session lookup / creation
// =========================================================================
mcp_rak_session_t* mcp_rak_session_manager_find(
    mcp_rak_session_manager_t* sm, const uint8_t ip[4], uint16_t port) {
    for (size_t i = 0; i < sm->session_count; ++i) {
        if (memcmp(sm->sessions[i]->ip, ip, 4) == 0 &&
            sm->sessions[i]->port == port)
            return sm->sessions[i];
    }
    return NULL;
}

mcp_rak_session_t* mcp_rak_session_manager_get_or_create(
    mcp_rak_session_manager_t* sm, const uint8_t ip[4], uint16_t port) {
    mcp_rak_session_t* s = mcp_rak_session_manager_find(sm, ip, port);
    if (s) return s;
    if (sm->session_count == sm->session_capacity) {
        size_t nc = sm->session_capacity == 0 ? 8 : sm->session_capacity * 2;
        mcp_rak_session_t** arr = (mcp_rak_session_t**)realloc(
            sm->sessions, nc * sizeof(mcp_rak_session_t*));
        if (!arr) return NULL;
        sm->sessions = arr;
        sm->session_capacity = nc;
    }
    mcp_rak_session_t* ns = (mcp_rak_session_t*)calloc(1, sizeof(*ns));
    if (!ns) return NULL;
    mcp_rak_session_init(ns, ip, port, 0, sm->server_id);
    sm->sessions[sm->session_count++] = ns;
    return ns;
}

// =========================================================================
// Send helpers
// =========================================================================
ssize_t mcp_rak_session_manager_send(mcp_rak_session_manager_t* sm,
                                       const uint8_t* data, size_t len,
                                       const uint8_t ip[4], uint16_t port) {
    if (!sm || !data || !ip) return -1;
    return mcp_udp_socket_send(&sm->socket, data, len, ip, port);
}

bool mcp_rak_session_manager_send_encapsulated(mcp_rak_session_manager_t* sm,
                                                const uint8_t ip[4], uint16_t port,
                                                const uint8_t* payload, size_t plen,
                                                uint8_t reliability,
                                                uint8_t order_channel) {
    if (!sm || !ip || !payload || plen == 0) return false;
    mcp_rak_session_t* s = mcp_rak_session_manager_find(sm, ip, port);
    if (!s || s->state < MCP_SESSION_STATE_CONNECTING_2) return false;
    // BUG FIX: Update last_activity_ms when we SEND data to the client.
    // The timeout check uses last_activity_ms to decide if the player is
    // still active. Without this, if the client stops sending game packets
    // (because it's overwhelmed by chunk data) but the server is actively
    // sending chunks TO the client, the server still times out the session.
    // PMMP updates last_activity on both send AND receive.
    //
    // NOTE (audit finding, left as-is — not part of the requested fixes):
    // this means a session's timeout clock resets on every outbound send,
    // including routine world upkeep the server sends regardless of
    // whether the player is doing anything (entity ticks, etc). Combined
    // with the "only non-PING game packets reset the timer" comment on
    // the receive side elsewhere in this file, the two send/receive paths
    // don't actually agree on what counts as activity. Not changing this
    // now since it makes timeouts more lenient, not the cause of the
    // reported early disconnects — but it's worth knowing about if
    // timeout behavior is revisited later.
    s->last_activity_ms = mcp_utils_time_ms();
    s->last_packet_sent_ms = s->last_activity_ms;
    s->diag_sent_packets++;
    return mcp_rak_session_send_payload(s, sm, payload, plen, reliability, order_channel);
}

// =========================================================================
// RakNet binary helpers (inline)
// =========================================================================
static void write_u8(uint8_t* b, size_t* oi, uint8_t v) {
    b[(*oi)++] = v;
}
static void write_u16_be(uint8_t* b, size_t* oi, uint16_t v) {
    b[(*oi)++] = (uint8_t)(v >> 8);
    b[(*oi)++] = (uint8_t)(v & 0xFF);
}
static void write_u24_le(uint8_t* b, size_t* oi, uint32_t v) {
    b[(*oi)++] = (uint8_t)(v & 0xFF);
    b[(*oi)++] = (uint8_t)((v >> 8) & 0xFF);
    b[(*oi)++] = (uint8_t)((v >> 16) & 0xFF);
}
static void write_u64_be(uint8_t* b, size_t* oi, uint64_t v) {
    for (int s = 56; s >= 0; s -= 8) b[(*oi)++] = (uint8_t)(v >> s);
}
static void write_u64_le(uint8_t* b, size_t* oi, uint64_t v) {
    for (int k = 0; k < 8; ++k)
        b[(*oi)++] = (uint8_t)(v >> (k * 8));
}
static void write_i64_be(uint8_t* b, size_t* oi, int64_t v) {
    write_u64_be(b, oi, (uint64_t)v);
}
static void write_bytes(uint8_t* b, size_t* oi, const void* src, size_t len) {
    memcpy(b + *oi, src, len);
    *oi += len;
}
static void write_magic(uint8_t* b, size_t* oi) {
    write_bytes(b, oi, RAKLIB_MAGIC_BYTES, 16);
}
// RakNet address format: [version=4][ip0][ip1][ip2][ip3][port BE]
static void write_address(uint8_t* b, size_t* oi,
                           const uint8_t ip[4], uint16_t port) {
    write_u8(b, oi, 4);       // IPv4
    write_bytes(b, oi, ip, 4);
    write_u16_be(b, oi, port);
}
// RakLib-style string: [u16_be length][bytes]
static void write_rakstring(uint8_t* b, size_t* oi, const char* str) {
    uint16_t len = str ? (uint16_t)strlen(str) : 0;
    write_u16_be(b, oi, len);
    if (len) write_bytes(b, oi, str, len);
}

// Read helpers
static uint8_t  rb_u8(const uint8_t* b, size_t len, size_t* i) {
    if (*i >= len) return 0;
    return b[(*i)++];
}
static uint16_t rb_u16_be(const uint8_t* b, size_t len, size_t* i) {
    if (*i + 2 > len) return 0;
    uint16_t v = ((uint16_t)b[*i] << 8) | b[*i+1];
    *i += 2;
    return v;
}
static uint32_t rb_u24_le(const uint8_t* b, size_t len, size_t* i) {
    if (*i + 3 > len) return 0;
    uint32_t v = b[*i] | ((uint32_t)b[*i+1] << 8) | ((uint32_t)b[*i+2] << 16);
    *i += 3;
    return v;
}

// =========================================================================
// FIX (audit priority 4): wrap-safe comparison for RakNet's 24-bit index
// spaces (datagram sequence number, reliable message index, and ordering
// index all share this format — 0x000000..0xFFFFFF, wrapping back to 0).
//
// Before this fix, every one of those three systems compared indices by
// plain (int32_t) subtraction/relational operators. That's correct as
// long as values stay far from the 24-bit boundary, but breaks exactly
// at the wrap: e.g. new=0x000001 arriving right after last=0xFFFFFE are
// three CONSECUTIVE values in the real 24-bit circular space, but
// (int32_t)1 - (int32_t)0xFFFFFE computes as a huge negative number —
// the code would treat that as "wildly out of order / stale" instead of
// "the very next expected value", and (depending on which of the three
// systems hit it) either silently stop advancing its own bookkeeping or
// silently reject the legitimate next packet. A session that stays
// connected long enough / busy enough to advance 16.7 million indices
// would hit this and go silent with no prior warning — matching the
// "sudden total silence, no gradual degradation" failure mode reported.
//
// These treat the 24-bit space as circular: "greater than" means "reached
// by counting forward fewer than half the space away", exactly like TCP
// sequence number comparison (RFC 1323) adapted to 24 bits instead of 32.
// NOTE: MCP_SEQ24_MASK itself now lives in Session.h — Session.c's send
// path needs it too (see the comment there), so it moved out of this
// file to avoid two independent, easily-desynced definitions of the
// same protocol constant.
// =========================================================================
#define MCP_SEQ24_HALF   0x00800000u

// True if `a` is strictly after `b` in the circular 24-bit space.
static inline bool seq24_greater(uint32_t a, uint32_t b) {
    uint32_t d = (a - b) & MCP_SEQ24_MASK;
    return d != 0 && d < MCP_SEQ24_HALF;
}
static inline bool seq24_less(uint32_t a, uint32_t b) {
    return seq24_greater(b, a);
}
// Forward circular distance from `b` to `a` (how many steps to count up
// from b to reach a). Only meaningful/reliable when that distance is
// less than half the space — by construction here it always is, because
// every caller below already rejected anything further than
// MCP_SESSION_WINDOW_SIZE / MCP_RELIABLE_WINDOW_SIZE away (both far
// smaller than the half-space threshold) before computing a distance.
static inline int32_t seq24_diff(uint32_t a, uint32_t b) {
    uint32_t d = (a - b) & MCP_SEQ24_MASK;
    if (d >= MCP_SEQ24_HALF) d -= (MCP_SEQ24_MASK + 1);
    return (int32_t)d;
}

static uint64_t rb_u64_be(const uint8_t* b, size_t len, size_t* i) {
    if (*i + 8 > len) return 0;
    uint64_t v = 0;
    for (int s = 56; s >= 0; s -= 8) v |= ((uint64_t)b[(*i)++] << s);
    return v;
}
static uint64_t rb_u64_le(const uint8_t* b, size_t len, size_t* i) {
    if (*i + 8 > len) return 0;
    uint64_t v = 0;
    for (int k = 0; k < 8; ++k)
        v |= (uint64_t)b[(*i)++] << (k * 8);
    return v;
}
static int64_t rb_i64_be(const uint8_t* b, size_t len, size_t* i) {
    return (int64_t)rb_u64_be(b, len, i);
}

// =========================================================================
// Build ACK packet (0xc0):
//   [0xc0] [u16_be record_count] { [single=0x01/range=0x00] [min 3LE] ([max 3LE]) }
// =========================================================================
static size_t build_ack_pkt(uint8_t* out, size_t cap,
                              const uint32_t* mins, const uint32_t* maxs, size_t n,
                              bool is_nack) {
    size_t oi = 0;
    if (oi + 3 + n * 7 > cap) return 0;
    out[oi++] = is_nack ? 0xa0 : 0xc0;
    out[oi++] = (uint8_t)(n >> 8);
    out[oi++] = (uint8_t)(n & 0xFF);
    for (size_t k = 0; k < n; ++k) {
        if (mins[k] == maxs[k]) {
            out[oi++] = 0x01; // single-seq record
            write_u24_le(out, &oi, mins[k]);
        } else {
            out[oi++] = 0x00; // range record
            write_u24_le(out, &oi, mins[k]);
            write_u24_le(out, &oi, maxs[k]);
        }
    }
    return oi;
}

// =========================================================================
// Build UNCONNECTED_PONG (0x1c)
// Format: [0x1c][time u64 LE][serverID u64 BE][magic 16][name rakstring]
// PMMP writes ping_time as LE (getLong = LE), server_id as BE (getLong = BE
// only for server fields). Mirror that exactly.
// =========================================================================
static size_t build_unconnected_pong(uint8_t* out, size_t cap,
                                      uint64_t ping_time, int64_t server_id,
                                      const char* name) {
    size_t oi = 0;
    out[oi++] = 0x1c;
    write_u64_le(out, &oi, ping_time);
    write_u64_be(out, &oi, (uint64_t)server_id);
    write_magic(out, &oi);
    write_rakstring(out, &oi, name);
    (void)cap;
    return oi;
}

// =========================================================================
// Build OPEN_CONNECTION_REPLY_1 (0x06)
// Format: [0x06][magic 16][serverID u64][security=0][mtu u16 BE]
// =========================================================================
static size_t build_ocr1_reply(uint8_t* out, size_t cap,
                                 int64_t server_id, uint16_t mtu) {
    size_t oi = 0;
    out[oi++] = 0x06;
    write_magic(out, &oi);
    write_u64_be(out, &oi, (uint64_t)server_id);
    out[oi++] = 0x00; // security = false
    write_u16_be(out, &oi, mtu);
    (void)cap;
    return oi;
}

// =========================================================================
// Build OPEN_CONNECTION_REPLY_2 (0x08)
// Format: [0x08][magic 16][serverID u64][clientIP][mtu u16 BE][security=0]
// =========================================================================
static size_t build_ocr2_reply(uint8_t* out, size_t cap,
                                 int64_t server_id,
                                 const uint8_t client_ip[4], uint16_t client_port,
                                 uint16_t mtu) {
    size_t oi = 0;
    out[oi++] = 0x08;
    write_magic(out, &oi);
    write_u64_be(out, &oi, (uint64_t)server_id);
    write_address(out, &oi, client_ip, client_port);
    write_u16_be(out, &oi, mtu);
    out[oi++] = 0x00; // security = false
    (void)cap;
    return oi;
}

// =========================================================================
// Build SERVER_HANDSHAKE_DataPacket (0x10) — sent encapsulated
// Format: [0x10][clientIP][short=0][10 x sysaddr][sendPing i64][sendPong i64]
// =========================================================================
static size_t build_server_handshake(uint8_t* out, size_t cap,
                                      const uint8_t client_ip[4], uint16_t client_port,
                                      int64_t send_ping) {
    static const uint8_t null_ip[4] = {0, 0, 0, 0};
    size_t oi = 0;
    out[oi++] = 0x10;
    write_address(out, &oi, client_ip, client_port);
    write_u16_be(out, &oi, 0); // short = 0 (unknown field, PMMP writes 0)
    // 10 system addresses (127.0.0.1:0 + 9x 0.0.0.0:0)
    static const uint8_t loopback[4] = {127, 0, 0, 1};
    write_address(out, &oi, loopback, 0);
    for (int i = 1; i < 10; ++i) write_address(out, &oi, null_ip, 0);
    write_i64_be(out, &oi, send_ping);
    write_i64_be(out, &oi, send_ping + 1000); // sendPong = sendPing + 1ms
    (void)cap;
    return oi;
}

// =========================================================================
// Decode OPEN_CONNECTION_REQUEST_1 (0x05)
// Layout: [id][magic 16][protocol u8][padding MTU bytes]
// MTU = total packet size (including UDP+IP headers which we don't see)
// Standard MCPE estimate: payload_size + 28 (20 IP + 8 UDP)
// =========================================================================
static bool decode_ocr1(const uint8_t* data, size_t len,
                          uint8_t* out_protocol, uint16_t* out_mtu) {
    if (len < 18) return false; // 1(id) + 16(magic) + 1(protocol) min
    // skip id(1) + magic(16)
    *out_protocol = data[17];
    // Remaining bytes after protocol are padding to probe MTU
    // MTU = len + 28 (IP header + UDP header not in payload)
    *out_mtu = (uint16_t)(len + 28);
    if (*out_mtu > RAKLIB_MAX_MTU) *out_mtu = RAKLIB_MAX_MTU;
    return true;
}

// =========================================================================
// Decode OPEN_CONNECTION_REQUEST_2 (0x07)
// Layout: [id][magic 16][serverAddress][mtu u16 BE][clientID i64]
// =========================================================================
static bool decode_ocr2(const uint8_t* data, size_t len,
                          uint16_t* out_mtu, int64_t* out_client_id,
                          uint16_t* out_server_port) {
    if (len < 34) return false;
    size_t i = 1 + 16; // skip id + magic
    // server address: [ver u8][ip 4 bytes][port u16 BE]
    uint8_t ver = data[i++];
    (void)ver;
    i += 4; // ip bytes
    *out_server_port = (uint16_t)((data[i] << 8) | data[i+1]); i += 2;
    // mtu
    *out_mtu = (uint16_t)((data[i] << 8) | data[i+1]); i += 2;
    if (*out_mtu > RAKLIB_MAX_MTU) *out_mtu = RAKLIB_MAX_MTU;
    // client id
    if (i + 8 > len) return false;
    *out_client_id = rb_i64_be(data, len, &i);
    return true;
}

// =========================================================================
// Decode RakNet DATA_PACKET (0x80..0x8F) header
// Returns payload offset, fills seq.
// =========================================================================
static bool decode_data_header(const uint8_t* data, size_t len,
                                 uint32_t* out_seq, size_t* out_payload_off) {
    if (len < 4) return false;
    // skip type byte (data[0])
    *out_seq = (uint32_t)data[1] | ((uint32_t)data[2] << 8) | ((uint32_t)data[3] << 16);
    *out_payload_off = 4;
    return true;
}

// =========================================================================
// Parse one encapsulated record from a DataPacket payload.
// Returns bytes consumed, fills payload ptr + len.
// Sets *out_done = true when no more records remain.
// If the packet is a split fragment, fills split info for reassembly.
// =========================================================================
static size_t parse_encap_record(const uint8_t* buf, size_t buf_len,
                                   const uint8_t** out_payload, size_t* out_plen,
                                   bool* out_done,
                                   bool* out_has_split, uint32_t* out_split_count,
                                   uint16_t* out_split_id, uint32_t* out_split_index,
                                   uint8_t* out_reliability,
                                   bool* out_is_reliable,
                                   uint32_t* out_message_index,
                                   bool* out_is_ordered,
                                   uint32_t* out_order_index,
                                   uint8_t* out_order_channel) {
    *out_done = true;
    *out_payload = NULL;
    *out_plen = 0;
    *out_has_split = false;
    *out_split_count = 0;
    *out_split_id = 0;
    *out_split_index = 0;
    *out_reliability = 0;
    *out_is_reliable = false;
    *out_message_index = 0;
    *out_is_ordered = false;
    *out_order_index = 0;
    *out_order_channel = 0;
    if (buf_len < 3) return 0;

    size_t i = 0;
    uint8_t flags    = buf[i++];
    uint8_t reliability = (flags >> 5) & 0x07;
    bool    has_split   = (flags & 0x10) != 0;
    *out_reliability = reliability;

    // bit-length of payload (u16 BE)
    uint16_t bit_len = (uint16_t)((buf[i] << 8) | buf[i+1]); i += 2;
    size_t   plen    = (bit_len + 7) / 8;

    // FIX #9: define reliable/ordered/sequenced explicitly against all
    // 8 PacketReliability values instead of only recognizing a subset —
    // encoder (build_datagram) and decoder must agree on exactly which
    // values carry which fields, or a reliability this server hasn't
    // wired through consistently would desync the two sides silently.
    //   0 UNRELIABLE                      — none of the below
    //   1 UNRELIABLE_SEQUENCED            — sequenceIndex only
    //   2 RELIABLE                        — messageIndex only
    //   3 RELIABLE_ORDERED                — messageIndex + orderIndex/chan
    //   4 RELIABLE_SEQUENCED              — messageIndex + sequenceIndex
    //   5 UNRELIABLE_WITH_ACK_RECEIPT     — none (ack receipt is local-only)
    //   6 RELIABLE_WITH_ACK_RECEIPT       — messageIndex only
    //   7 RELIABLE_ORDERED_WITH_ACK_RECEIPT — messageIndex + orderIndex/chan
    bool is_reliable = (reliability == MCP_RELIABILITY_RELIABLE ||
                        reliability == MCP_RELIABILITY_RELIABLE_ORDERED ||
                        reliability == MCP_RELIABILITY_RELIABLE_SEQUENCED ||
                        reliability == MCP_RELIABILITY_RELIABLE_WITH_ACK_RECEIPT ||
                        reliability == MCP_RELIABILITY_RELIABLE_ORDERED_WITH_ACK_RECEIPT);
    bool is_ordered  = (reliability == MCP_RELIABILITY_RELIABLE_ORDERED ||
                        reliability == MCP_RELIABILITY_RELIABLE_ORDERED_WITH_ACK_RECEIPT);
    bool is_sequenced = (reliability == MCP_RELIABILITY_UNRELIABLE_SEQUENCED ||
                         reliability == MCP_RELIABILITY_RELIABLE_SEQUENCED);
    *out_is_reliable = is_reliable;

    // messageIndex (3-byte LE) — present for every is_reliable value above.
    if (is_reliable) {
        if (i + 3 > buf_len) return 0;
        // FIX #2: actually decode this instead of just skipping past it.
        // This is the reliable MESSAGE index — distinct from both the
        // datagram sequence number and the ordering index. Needed to
        // give reliable messages their own duplicate/staleness window,
        // independent of which physical datagram (and possibly which
        // retransmit) carried them.
        uint32_t mi = (uint32_t)buf[i] | ((uint32_t)buf[i+1] << 8) |
                      ((uint32_t)buf[i+2] << 16);
        i += 3;
        *out_message_index = mi;
    }

    // sequenceIndex (3-byte LE) for SEQUENCED
    if (is_sequenced) {
        if (i + 3 > buf_len) return 0;
        i += 3;
    }

    // orderIndex (3-byte LE) + orderChannel (1-byte) for ORDERED
    if (is_ordered) {
        if (i + 4 > buf_len) return 0;
        // FIX: actually decode these instead of just skipping past them.
        // Without this, the caller has no way to know a packet needs to
        // wait for earlier packets on its channel.
        uint32_t oi = (uint32_t)buf[i] | ((uint32_t)buf[i+1] << 8) |
                      ((uint32_t)buf[i+2] << 16);
        i += 3;
        uint8_t chan = buf[i++] & 0x1F;
        *out_is_ordered    = true;
        *out_order_index   = oi;
        *out_order_channel = chan;
    }

    // split info
    if (has_split) {
        if (i + 10 > buf_len) return 0;
        *out_has_split = true;
        *out_split_count = ((uint32_t)buf[i] << 24) | ((uint32_t)buf[i+1] << 16) |
                           ((uint32_t)buf[i+2] << 8) | buf[i+3]; i += 4;
        *out_split_id = ((uint16_t)buf[i] << 8) | buf[i+1]; i += 2;
        *out_split_index = ((uint32_t)buf[i] << 24) | ((uint32_t)buf[i+1] << 16) |
                           ((uint32_t)buf[i+2] << 8) | buf[i+3]; i += 4;
    }

    if (i + plen > buf_len) return 0;
    *out_payload = buf + i;
    *out_plen    = plen;
    *out_done    = false;
    return i + plen;
}

// =========================================================================
// FIX #2: reliable message-index receive window.
//
// This is intentionally separate from recv_window (which tracks RakNet
// transport datagram sequence numbers). The reliable message index
// identifies a fiable MESSAGE, independent of which datagram (original
// send or retransmit) carried it — a retransmit gets a brand new sequence
// number but keeps the SAME message index. Without this check, a
// retransmitted reliable message has no way to be recognized as "already
// delivered" if it arrives after the original (which the transport-level
// window would already have accepted as a new datagram).
//
// Returns true if the message should be processed, false if it's a
// duplicate or too far outside the window to be trusted.
// =========================================================================
static bool reliable_index_accept(mcp_rak_session_t* s, uint32_t message_index) {
    if (s->last_reliable_index < 0) {
        // FIX: first reliable message this session has ever seen — center
        // the window on WHATEVER index it actually is, not unconditionally
        // 0. A message_index doesn't have to start at 0 (mirrors the same
        // "first datagram sequence number needn't be 0" case for the
        // transport-level window above); starting reliable_window_start
        // at 0 while the real first index is, say, 500000, would make the
        // very first message compute as astronomically far outside its
        // own window.
        s->last_reliable_index = (int32_t)message_index;
        s->reliable_window_start = message_index;
    }

    // FIX (priority 4): wrap-safe circular check instead of plain
    // subtraction — message_index is a 24-bit wire value with the same
    // wrap-around hazard as the datagram sequence number (see seq24_*
    // above). A plain `message_index - reliable_window_start` computed
    // near the 24-bit boundary could wrap to a small-looking value and
    // be wrongly accepted as "inside the window" when it's actually a
    // stale index from before the wrap.
    if (!seq24_less(message_index, s->reliable_window_start) &&
        seq24_diff(message_index, s->reliable_window_start) < MCP_RELIABLE_WINDOW_SIZE) {
        uint32_t slot = message_index % MCP_RELIABLE_WINDOW_SIZE;
        if (s->reliable_recv_window[slot]) return false; // duplicate
        s->reliable_recv_window[slot] = true;
        if (seq24_greater(message_index, (uint32_t)s->last_reliable_index)) {
            s->last_reliable_index = (int32_t)message_index;
        }
        // Slide the window forward if we've accepted enough of the front
        // half — mirrors the same slide strategy already used for the
        // transport-level recv_window below.
        // FIX: circular comparison — a plain >= here has the same
        // wrap hazard as everywhere else message_index is compared.
        if (seq24_diff(message_index, s->reliable_window_start) >= (int32_t)(MCP_RELIABLE_WINDOW_SIZE / 2)) {
            uint32_t new_start = (message_index - MCP_RELIABLE_WINDOW_SIZE / 2) & MCP_SEQ24_MASK;
            // FIX: walk the cleared range in wrap-safe steps instead of a
            // plain `for (m = old_start; m < new_start; ++m)`, which
            // silently does nothing (or the wrong thing) if new_start
            // wrapped below old_start numerically while still being
            // "ahead" in the circular space.
            uint32_t m = s->reliable_window_start;
            while (m != new_start) {
                s->reliable_recv_window[m % MCP_RELIABLE_WINDOW_SIZE] = false;
                m = (m + 1) & MCP_SEQ24_MASK;
            }
            s->reliable_window_start = new_start;
        }
        return true;
    }

    // Outside the window: either far in the past (a stale retransmit of
    // something already delivered and long since slid out) or
    // implausibly far in the future (protocol error / corrupt index).
    // Either way, don't hand it to the game layer.
    return false;
}

// Forward declaration — used inside handle_encapsulated() (CLIENT_DISCONNECT
// handling) before its full definition later in this file.
static void log_session_diagnostics(const mcp_rak_session_t* s, const char* reason);

// Forward declaration — deliver_ordered() below calls this once a packet
// is confirmed to be next-in-order (or doesn't need ordering at all).
static void handle_encapsulated(mcp_rak_session_manager_t* sm,
                                  mcp_rak_session_t* s,
                                  const uint8_t* payload, size_t plen);

// =========================================================================
// FIX: RELIABLE_ORDERED delivery gate.
//
// RakNet's contract for an ordered channel: packets are handed to the
// application in the exact sequence they were SENT (by order_index),
// never in raw arrival order. Before this fix the server ignored
// order_index entirely and called handle_encapsulated() the instant a
// record was parsed — correct only as long as the network never
// reorders datagrams, which plain UDP does not guarantee even without
// any packet loss.
//
// This holds back a packet that arrives ahead of the channel's expected
// index, and replays the held queue in order as soon as the gap closes
// (either the missing packet arrives, or — for datagrams that were
// never ordered/reliable — there's nothing to wait for).
// =========================================================================
static void deliver_ordered(mcp_rak_session_manager_t* sm,
                              mcp_rak_session_t* s,
                              bool is_ordered, uint32_t order_index,
                              uint8_t order_channel,
                              const uint8_t* payload, size_t plen) {
    if (!is_ordered) {
        // UNRELIABLE / RELIABLE (unordered) — deliver immediately, exactly
        // as before. Only RELIABLE_ORDERED needs the gate.
        handle_encapsulated(sm, s, payload, plen);
        return;
    }

    uint8_t chan = order_channel & 0x1F;

    if (order_index != s->recv_order_index[chan]) {
        // Arrived ahead of what we're expecting on this channel — hold it
        // instead of delivering out of sequence.
        // FIX (priority 4): order_index is also a 24-bit wire value — use
        // the same wrap-safe comparison as sequence number and reliable
        // message index instead of plain `<`, which breaks exactly at the
        // 24-bit boundary the same way documented in seq24_* above.
        if (seq24_less(order_index, s->recv_order_index[chan])) {
            // Stale/duplicate (retransmit of something already delivered).
            return;
        }
        // FIX #3-A: reject if this exact order_index is already sitting
        // in the hold buffer. Without this check, a retransmit of a
        // still-pending ordered packet would claim a SECOND slot for the
        // same index — wasting capacity and, when the gap finally
        // closes, delivering that payload to the game layer twice.
        for (int k = 0; k < MCP_ORDER_HOLD_CAP; ++k) {
            if (s->order_hold[chan][k].used &&
                s->order_hold[chan][k].order_index == order_index) {
                return; // already holding this index — ignore the duplicate
            }
        }
        bool stored = false;
        for (int k = 0; k < MCP_ORDER_HOLD_CAP; ++k) {
            if (!s->order_hold[chan][k].used) {
                uint8_t* copy = (uint8_t*)malloc(plen);
                if (!copy) {
                    // FIX #3-B: malloc failed — do NOT mark the slot used.
                    // The old code set used=true with data=NULL regardless,
                    // permanently wasting that slot (it could never match
                    // on delivery since held_data would always be NULL,
                    // but it would never free up either). Simply drop this
                    // packet; its own retransmit will give us another
                    // chance to hold it once memory pressure clears.
                    break;
                }
                memcpy(copy, payload, plen);
                s->order_hold[chan][k].used        = true;
                s->order_hold[chan][k].order_index = order_index;
                s->order_hold[chan][k].data        = copy;
                s->order_hold[chan][k].len         = plen;
                s->order_hold[chan][k].stored_ms   = mcp_utils_time_ms();
                stored = true;
                // Diagnostics: track how full this ever gets.
                uint32_t used_now = 0;
                for (int kk = 0; kk < MCP_ORDER_HOLD_CAP; ++kk)
                    if (s->order_hold[chan][kk].used) used_now++;
                if (used_now > s->diag_order_hold_peak)
                    s->diag_order_hold_peak = used_now;
                break;
            }
        }
        if (!stored) {
            // FIX #3-D: buffer full (or malloc failed on the last free
            // slot) — this used to drop the packet with no record of it
            // happening at all. Now it's at least counted, so a
            // disconnect log can show whether ordered-channel backlog was
            // a factor.
            s->diag_order_hold_full++;
        }
        return;
    }

    // This is exactly the packet the channel is waiting for — deliver it,
    // then drain any held packets that are now next in line.
    s->recv_order_index[chan] = (s->recv_order_index[chan] + 1) & MCP_SEQ24_MASK;
    s->recv_order_advanced_ms[chan] = mcp_utils_time_ms();
    handle_encapsulated(sm, s, payload, plen);

    bool advanced = true;
    while (advanced) {
        advanced = false;
        for (int k = 0; k < MCP_ORDER_HOLD_CAP; ++k) {
            if (s->order_hold[chan][k].used &&
                s->order_hold[chan][k].order_index == s->recv_order_index[chan]) {
                uint8_t* held_data = s->order_hold[chan][k].data;
                size_t   held_len  = s->order_hold[chan][k].len;
                s->order_hold[chan][k].used = false;
                s->order_hold[chan][k].data = NULL;
                s->recv_order_index[chan] = (s->recv_order_index[chan] + 1) & MCP_SEQ24_MASK;
                s->recv_order_advanced_ms[chan] = mcp_utils_time_ms();
                if (held_data) {
                    handle_encapsulated(sm, s, held_data, held_len);
                    free(held_data);
                }
                advanced = true;
                break; // restart the scan — recv_order_index moved
            }
        }
    }
}

// =========================================================================
// FIX #3-C: evict order_hold entries that have been waiting too long for
// their gap to close. Called from the per-session tick sweep alongside
// the split-slot timeout eviction. A held packet only ever gets stuck
// this way if the specific missing packet it's waiting on (and every one
// of ITS retransmits) never arrives — at that point the channel is
// permanently stalled unless something forces it to skip ahead, which
// this does by dropping the held backlog and re-syncing
// recv_order_index to resume from the next lowest still-held index (or
// leaving it as-is if nothing is held at all).
// =========================================================================
static void expire_order_holds(mcp_rak_session_t* s, uint64_t now) {
    for (int chan = 0; chan < 32; ++chan) {
        bool any_expired = false;
        for (int k = 0; k < MCP_ORDER_HOLD_CAP; ++k) {
            if (s->order_hold[chan][k].used &&
                (now - s->order_hold[chan][k].stored_ms) > 10000) {
                any_expired = true;
                break;
            }
        }
        if (!any_expired) continue;

        // Something in this channel has been stuck long enough to give
        // up on. Free every expired slot. If the CHANNEL's own next
        // expected index (recv_order_index) is one of the ones just
        // freed, step forward exactly one — the same "drain" logic
        // deliver_ordered() already uses when a real packet arrives —
        // so any packet already waiting right behind it gets delivered
        // immediately instead of waiting out its own separate timeout.
        // This only ever unblocks the specific index that was actually
        // abandoned; anything the channel is waiting on that hasn't
        // reached 10s yet is left completely untouched.
        bool expected_was_freed = false;
        for (int k = 0; k < MCP_ORDER_HOLD_CAP; ++k) {
            if (s->order_hold[chan][k].used &&
                (now - s->order_hold[chan][k].stored_ms) > 10000) {
                if (s->order_hold[chan][k].order_index == s->recv_order_index[chan]) {
                    // This shouldn't normally happen (a slot holding
                    // exactly the currently-expected index would have
                    // been delivered already by deliver_ordered's drain
                    // loop), but handle it defensively rather than assume.
                    expected_was_freed = true;
                }
                if (s->order_hold[chan][k].data) free(s->order_hold[chan][k].data);
                s->order_hold[chan][k].used = false;
                s->order_hold[chan][k].data = NULL;
            }
        }
        if (expected_was_freed) {
            s->recv_order_index[chan] = (s->recv_order_index[chan] + 1) & MCP_SEQ24_MASK;
            s->recv_order_advanced_ms[chan] = mcp_utils_time_ms();
        }

        // FIX: the case expected_was_freed does NOT cover — a gap where
        // the missing packet (and every retransmit of it) was lost
        // outright, so nothing is even sitting in order_hold waiting for
        // it. That's invisible to the loop above (there's no slot to
        // expire), but it's exactly as capable of stalling this channel
        // forever as the case that loop does handle. Detect it directly:
        // if this channel hasn't advanced in over 10s AND there's at
        // least one packet held further ahead (proof something DID get
        // sent after the gap — the client isn't simply idle), the packet
        // occupying recv_order_index itself must be considered lost.
        // Skip past it so the channel can resume from whatever's next,
        // rather than waiting on an index that will never arrive.
        if ((now - s->recv_order_advanced_ms[chan]) > 10000) {
            bool something_waiting_ahead = false;
            for (int k = 0; k < MCP_ORDER_HOLD_CAP; ++k) {
                if (s->order_hold[chan][k].used) { something_waiting_ahead = true; break; }
            }
            if (something_waiting_ahead) {
                s->recv_order_index[chan] = (s->recv_order_index[chan] + 1) & MCP_SEQ24_MASK;
                s->recv_order_advanced_ms[chan] = now;
                s->diag_order_forced_skip++;
            }
        }
        // NOTE: if NOTHING is held ahead either, this channel simply has
        // no traffic right now — there's nothing to skip ahead TO, so
        // recv_order_index is correctly left exactly where it is. This
        // is not a stall; it's an idle ordered channel, which is normal
        // whenever the client isn't sending anything on it.
        // NOTE: the channel's expectation intentionally does NOT jump
        // past a gap left by a MISSING packet that was never held at
        // all (the common case — nothing arrived out of order because
        // nothing later has arrived yet either). There is nothing to
        // safely skip in that case: the missing packet's own retransmit
        // is still the correct way to eventually fill it, and skipping
        // ahead speculatively risks delivering a later packet before an
        // earlier one that's simply still in transit.
    }
}

// =========================================================================
// Handle an encapsulated application-layer packet from an established
// or connecting session. Called after the DATA_PACKET header is stripped.
// =========================================================================
static void handle_encapsulated(mcp_rak_session_manager_t* sm,
                                  mcp_rak_session_t* s,
                                  const uint8_t* payload, size_t plen) {
    if (plen < 1) return;
    uint8_t pid = payload[0];
    uint8_t dgram[RAKLIB_MAX_MTU + 128];
    size_t  dgram_len = 0;
    char    ipStr[20];

    if (s->state == MCP_SESSION_STATE_CONNECTING_2) {
        // ---- CLIENT_CONNECT (0x09) ----
        if (pid == 0x09 && plen >= 17) {
            size_t i = 1;
            int64_t client_id = rb_i64_be(payload, plen, &i);
            int64_t send_ping = (int64_t)rb_u64_be(payload, plen, &i);
            s->client_id = client_id;

            // Reply: SERVER_HANDSHAKE (0x10) — UNRELIABLE encapsulated
            dgram_len = build_server_handshake(dgram, sizeof(dgram),
                                                s->ip, s->port, send_ping);
            // Wrap in DataPacket_4 (0x84) with UNRELIABLE — always fits, no split needed
            uint8_t dp[RAKLIB_MAX_MTU + 64];
            size_t  dp_len = mcp_rak_session_build_datagram(s, dp, sizeof(dp),
                                dgram, dgram_len, MCP_RELIABILITY_UNRELIABLE, 0,
                                false, 0, 0, 0);
            // DIAGNOSTIC (temporary — remove once the "connects but times
            // out, 0 sent_packets logged" report is confirmed fixed):
            // this send path bypasses mcp_rak_session_manager_send_
            // encapsulated(), which is the ONLY function that increments
            // diag_sent_packets. That means the existing per-session
            // diagnostic dump can show sent_packets=0 even when
            // SERVER_HANDSHAKE genuinely went out over the socket — the
            // counter simply never saw it. Logging directly here removes
            // that blind spot and tells us definitively whether the
            // server-side send actually happens, what size it built, and
            // what raw sendto() returned, for the very packet that
            // determines whether the client ever completes the RakNet
            // handshake.
            {
                char ipStr2[20];
                snprintf(ipStr2, sizeof(ipStr2), "%u.%u.%u.%u",
                         s->ip[0], s->ip[1], s->ip[2], s->ip[3]);
                char buf[192];
                snprintf(buf, sizeof(buf),
                    "[RakLib][diag] CLIENT_CONNECT from %s:%u — "
                    "dgram_len=%zu dp_len=%zu",
                    ipStr2, s->port, dgram_len, dp_len);
                if (sm->instance && sm->instance->on_raw_packet) {
                    // Reuse on_raw_packet purely as a text sink isn't
                    // right either — fall through to stderr instead so
                    // this never depends on a callback being wired up.
                }
                fprintf(stderr, "%s\n", buf);
            }
            if (dp_len > 0) {
                ssize_t sr = mcp_rak_session_manager_send(sm, dp, dp_len, s->ip, s->port);
                fprintf(stderr,
                    "[RakLib][diag] SERVER_HANDSHAKE sendto() returned %zd "
                    "(dp_len=%zu)\n", (ssize_t)sr, dp_len);
            } else {
                fprintf(stderr,
                    "[RakLib][diag] build_datagram for SERVER_HANDSHAKE "
                    "returned 0 — nothing sent!\n");
            }
            return;
        }

        // DIAGNOSTIC (temporary): anything else received while still in
        // CONNECTING_2 was previously dropped completely silently — no
        // log, no response, nothing. If the client is retrying
        // CLIENT_CONNECT with a slightly different payload shape (e.g.
        // an extra trailing byte some client builds add) it would fail
        // the `plen >= 17` + pid check above and land here instead,
        // which would explain recv_packets climbing to 17 while the
        // server stays completely silent — every retry silently missing
        // the 0x09 branch above.
        if (pid != 0x13 && pid != 0x15) {
            fprintf(stderr,
                "[RakLib][diag] Unhandled packet in CONNECTING_2: pid=0x%02x "
                "plen=%zu from %u.%u.%u.%u:%u\n",
                pid, plen, s->ip[0], s->ip[1], s->ip[2], s->ip[3], s->port);
        }

        // ---- CLIENT_HANDSHAKE (0x13) ----
        if (pid == 0x13) {
            size_t i = 1;
            // clientAddress: [ver][ip 4][port 2]
            uint8_t ver = rb_u8(payload, plen, &i);
            (void)ver;
            i += 4; // ip
            uint16_t srv_port = rb_u16_be(payload, plen, &i);
            (void)srv_port;

            // portChecking: srv_port must match bind_port (or disabled)
            // PMMP default: portChecking enabled
            if (srv_port == sm->bind_port || true /* portChecking off for MCPE */) {
                s->state = MCP_SESSION_STATE_CONNECTED;
                // Notify upper layer
                if (sm->instance && sm->instance->on_open_session) {
                    snprintf(ipStr, sizeof(ipStr), "%u.%u.%u.%u",
                             s->ip[0], s->ip[1], s->ip[2], s->ip[3]);
                    sm->instance->on_open_session(sm->instance->impl,
                                                    ipStr, s->port, s->client_id);
                }
            }
            return;
        }

        // ---- CLIENT_DISCONNECT (0x15) ----
        if (pid == 0x15) {
            if (sm->instance && sm->instance->on_close_session) {
                snprintf(ipStr, sizeof(ipStr), "%u.%u.%u.%u",
                         s->ip[0], s->ip[1], s->ip[2], s->ip[3]);
                sm->instance->on_close_session(sm->instance->impl,
                                                 ipStr, s->port, "client disconnect");
            }
            s->is_disconnecting = true;  // Bug 2 fix: cleanup next tick
            return;
        }
    }

    if (s->state == MCP_SESSION_STATE_CONNECTED) {
        // PING (0x00) is a RakNet-level keepalive. It does NOT mean the
        // client is actively playing — MCPE keeps sending PINGs even after
        // showing "Desconexion del servidor" to the user (the app stays
        // alive in Android's background for ~60-90 seconds).
        //
        // FIX: Only update last_activity_ms for GAME packets (not PING).
        // This way, if the client stops sending game packets but keeps
        // sending PINGs (because the app is still alive in background),
        // the timeout fires correctly after 15 seconds.
        //
        // PING is still answered with PONG to keep RakNet happy, but
        // it doesn't reset the game-level activity timer.
        bool is_ping = (pid == 0x00);

        if (!is_ping) {
            s->last_activity_ms = mcp_utils_time_ms();
            s->last_game_packet_received_ms = s->last_activity_ms;
            s->is_active = true;
        } else {
            s->last_ping_received_ms = mcp_utils_time_ms();
        }

        // ---- PING (0x00) — internal keep-alive ----
        if (is_ping && plen >= 9) {
            size_t i = 1;
            int64_t ping_id = (int64_t)rb_u64_be(payload, plen, &i);
            // Reply: PONG (0x03)
            size_t oi = 0;
            dgram[oi++] = 0x03;
            write_i64_be(dgram, &oi, ping_id);
            dgram_len = oi;
            uint8_t dp[64];
            size_t dp_len = mcp_rak_session_build_datagram(s, dp, sizeof(dp),
                                dgram, dgram_len, MCP_RELIABILITY_UNRELIABLE, 0,
                                false, 0, 0, 0);
            if (dp_len > 0)
                mcp_rak_session_manager_send(sm, dp, dp_len, s->ip, s->port);
            return;
        }

        // ---- CLIENT_DISCONNECT (0x15) ----
        if (pid == 0x15) {
            log_session_diagnostics(s, "CLIENT_DISCONNECT");
            if (sm->instance && sm->instance->on_close_session) {
                snprintf(ipStr, sizeof(ipStr), "%u.%u.%u.%u",
                         s->ip[0], s->ip[1], s->ip[2], s->ip[3]);
                sm->instance->on_close_session(sm->instance->impl,
                                                 ipStr, s->port, "client disconnect");
            }
            s->is_disconnecting = true;  // Bug 2 fix: cleanup next tick
            return;
        }

        // ---- Any other packet → forward to game layer ----
        if (sm->instance && sm->instance->on_packet_receive) {
            snprintf(ipStr, sizeof(ipStr), "%u.%u.%u.%u",
                     s->ip[0], s->ip[1], s->ip[2], s->ip[3]);
            sm->instance->on_packet_receive(sm->instance->impl,
                                              ipStr, s->port, payload, plen);
        }
    }
}

// =========================================================================
// Handle a received DATA_PACKET (0x80..0x8F)
// =========================================================================
static void handle_data_packet(mcp_rak_session_manager_t* sm,
                                  mcp_rak_session_t* s,
                                  const uint8_t* data, size_t len) {
    uint32_t seq;
    size_t   payload_off;
    if (!decode_data_header(data, len, &seq, &payload_off)) return;

    // Sequence-number / window logic (mirrors PMMP Session.php).
    // FIX (priority 4): use wrap-safe circular comparison instead of
    // plain int32_t relational operators — see seq24_* above for why
    // the old comparisons broke exactly at the 24-bit wrap boundary.
    if (s->last_recv_seq < 0) {
        // FIX (case G — first datagram this session ever receives, and
        // its sequence number need not be 0; different sessions/clients
        // can start their sequence counters anywhere). Accept whatever
        // the first value is and center the window on it, generating no
        // NACKs for "missing" numbers before it — there's nothing to
        // recover, they were simply never part of this session.
        s->last_recv_seq = (int32_t)seq; // stored only as a bit pattern now
        s->window_start  = seq; // will be re-centered below via diff==0 path
    } else {
        uint32_t last = (uint32_t)s->last_recv_seq & MCP_SEQ24_MASK;
        if (seq24_less(seq, s->window_start) ||
            !seq24_less(seq, s->window_end)) {
            return; // out of window — drop
        }
        (void)last;
    }
    uint32_t slot = seq % MCP_SESSION_WINDOW_SIZE;
    if (s->recv_window[slot]) return; // duplicate

    uint32_t lastu = (uint32_t)s->last_recv_seq & MCP_SEQ24_MASK;
    int32_t diff = (s->last_recv_seq < 0) ? 1 : seq24_diff(seq, lastu);
    mcp_rak_session_queue_ack(s, seq);
    s->recv_window[slot] = true;

    if (diff != 1) {
        // Gap: queue NACKs for missing sequences.
        // FIX: cap the gap span. A legitimate gap from real packet loss
        // is at most a handful of sequence numbers; a gap anywhere near
        // MCP_SESSION_WINDOW_SIZE would already have been rejected by the
        // window check above in normal operation, but guarding here too
        // means a single bad datagram can never force this loop to spin
        // for an unbounded number of iterations.
        if (diff > 0 && diff <= (int32_t)MCP_SESSION_WINDOW_SIZE) {
            // FIX: walk the gap in 24-bit-wrap-safe steps instead of a
            // plain int32_t range — a gap that straddles the wrap
            // boundary (e.g. last=0xFFFFF0, seq=0x000005) must still
            // generate NACKs for 0xFFFFF1..0xFFFFFF, 0x000000..0x000004,
            // which a naive `for (m = last+1; m < seq; ++m)` over
            // mismatched-sign int32_t values would not visit correctly.
            uint32_t m = (lastu + 1) & MCP_SEQ24_MASK;
            for (int32_t step = 0; step < diff - 1; ++step) {
                uint32_t wslot = m % MCP_SESSION_WINDOW_SIZE;
                if (!s->recv_window[wslot])
                    mcp_rak_session_queue_nack(s, m);
                m = (m + 1) & MCP_SEQ24_MASK;
            }
        }
    }
    if (diff >= 1) {
        // BUG CRÍTICO CONFIRMADO (evidencia: dos server.log reales
        // distintos, ambos con recv_packets=2048 exactos antes del
        // silencio total). recv_window[] marks a slot true on receipt
        // (line above) but — unlike reliable_recv_window, which DOES
        // clear stale slots when its window slides (see the `= false`
        // a few lines up in reliable_index_accept) — this transport-level
        // window never cleared anything. Once a slot was set true, it
        // stayed true forever. With MCP_SESSION_WINDOW_SIZE=2048 and
        // `seq % 2048` picking the slot, the very first datagram whose
        // sequence number wraps back to an already-used slot — i.e.
        // exactly the 2049th datagram received (seq 2048, since seq 0
        // claimed slot 0) — collides with recv_window[0] still being
        // true from the session's very first packet, and gets rejected
        // as "duplicate" at the `return` a few lines above, even though
        // it's a brand new, legitimate datagram. Every datagram after
        // that keeps colliding the same way, and since that rejection
        // happens BEFORE diag_recv_packets++ / last_packet_received_ms
        // update, the connection goes read-side-silent from the
        // server's point of view — explaining both the exact
        // recv_packets=2048 ceiling seen in two separate real sessions,
        // and the subsequent 30s-of-total-silence-then-TIMEOUT pattern,
        // with no NACKs, no recovery pressure, and no split/order_hold
        // activity to show for it (matching every diagnostic field in
        // both logs).
        //
        // Fix: clear the slots that just fell OUT of the window as it
        // slides forward — mirroring exactly what reliable_index_accept()
        // already does correctly for its own window. A slot only stays
        // legitimately "true" while its sequence number is still inside
        // [window_start, window_end); anything that falls behind
        // window_start as the window advances must be reset so a later
        // datagram that happens to hash to the same slot isn't mistaken
        // for the old one.
        uint32_t old_start = s->window_start;
        uint32_t new_start = (seq - MCP_SESSION_WINDOW_SIZE / 2) & MCP_SEQ24_MASK;
        uint32_t m = old_start;
        while (m != new_start) {
            s->recv_window[m % MCP_SESSION_WINDOW_SIZE] = false;
            m = (m + 1) & MCP_SEQ24_MASK;
        }
        s->last_recv_seq = (int32_t)seq;
        s->window_start  = new_start;
        s->window_end    = (seq + MCP_SESSION_WINDOW_SIZE / 2) & MCP_SEQ24_MASK;
    }
    // NOTE: do NOT update last_activity_ms here. This function handles
    // the RakNet DATA_PACKET level (sequence numbers, ACK/NACK). The
    // actual game activity is tracked in handle_encapsulated() which
    // only resets the timer for non-PING game packets. This prevents
    // background PINGs and ACK traffic from keeping zombie sessions
    // alive for 60+ seconds after the client has stopped playing.
    s->is_active = true;
    s->diag_recv_packets++;
    s->last_packet_received_ms = mcp_utils_time_ms();
    const uint8_t* ptr = data + payload_off;
    size_t         rem = len  - payload_off;
    while (rem > 0) {
        const uint8_t* epayload;
        size_t eplen;
        bool   done;
        bool   has_split = false;
        uint32_t split_count = 0, split_index = 0;
        uint16_t split_id = 0;
        uint8_t  reliability = 0;
        bool     is_reliable = false;
        uint32_t message_index = 0;
        bool     is_ordered = false;
        uint32_t order_index = 0;
        uint8_t  order_channel = 0;

        size_t consumed = parse_encap_record(ptr, rem, &epayload, &eplen, &done,
                                              &has_split, &split_count, &split_id, &split_index,
                                              &reliability, &is_reliable, &message_index,
                                              &is_ordered, &order_index, &order_channel);
        if (consumed == 0 || done) break;

        // BUG CRÍTICO CONFIRMADO (server.log real: split_completed=1 pero
        // el resto de la sesión sospechosamente silenciosa en tráfico
        // fiable — ver el hallazgo completo en el comentario de
        // reliable_index_accept() más abajo). In RakNet's real reference
        // implementation, a fragmented message gets exactly ONE
        // reliableMessageNumber for the whole logical message — every
        // physical fragment on the wire carries that SAME index (fixed
        // once, before splitting, in ReliabilityLayer::Send). That part
        // of this codebase's earlier comment was correct.
        //
        // The bug was applying reliable_index_accept()'s duplicate check
        // PER FRAGMENT, before reassembly, instead of once for the
        // reassembled message. Since every fragment after the first
        // shares the same message_index, reliable_index_accept() marked
        // it "already seen" on fragment 0 and silently rejected every
        // fragment after that as a duplicate — the split_slot would sit
        // forever short of slot->count, never complete, and eventually
        // get evicted by the 10s stale-split sweep with the chunk (or
        // whatever it was) simply gone. No error, no log — indistinguishable
        // from network silence, which matches exactly what's been
        // observed: real gameplay traffic right up to a sudden, total,
        // unexplained stop.
        //
        // Fix: skip the reliable_index_accept() check here for anything
        // that's part of a split; the check runs ONCE on the reassembled
        // payload instead (see the split-completion branch below), which
        // is the point at which "duplicate of an already-delivered
        // message" is actually the right question to ask.
        if (is_reliable && !has_split && !reliable_index_accept(s, message_index)) {
            ptr += consumed;
            rem -= consumed;
            continue;
        }

        if (has_split && split_count > 0 && split_count <= MCP_MAX_SPLIT_SEGMENTS) {
            // FIX #1: find the slot already tracking this split_id, or
            // claim a free one, or — if every slot is occupied by a
            // DIFFERENT split_id — evict only the least-recently-touched
            // one. This never discards an unrelated split just because a
            // new split_id showed up, which the old single-slot code did
            // unconditionally.
            mcp_split_store_t* slot = NULL;
            for (int si = 0; si < MCP_MAX_CONCURRENT_SPLITS; ++si) {
                if (s->split_slots[si].in_use && s->split_slots[si].split_id == split_id) {
                    slot = &s->split_slots[si];
                    break;
                }
            }
            if (!slot) {
                for (int si = 0; si < MCP_MAX_CONCURRENT_SPLITS; ++si) {
                    if (!s->split_slots[si].in_use) { slot = &s->split_slots[si]; break; }
                }
            }
            if (!slot) {
                // All slots occupied by distinct, still-incomplete splits —
                // evict the one that hasn't seen a fragment in the longest
                // time (almost certainly the stalest / most likely dead).
                int oldest = 0;
                for (int si = 1; si < MCP_MAX_CONCURRENT_SPLITS; ++si) {
                    if (s->split_slots[si].last_fragment_ms < s->split_slots[oldest].last_fragment_ms)
                        oldest = si;
                }
                for (uint32_t k = 0; k < s->split_slots[oldest].count && k < MCP_MAX_SPLIT_SEGMENTS; ++k) {
                    if (s->split_slots[oldest].segments[k]) {
                        free(s->split_slots[oldest].segments[k]);
                        s->split_slots[oldest].segments[k] = NULL;
                    }
                }
                s->diag_split_evicted++;
                slot = &s->split_slots[oldest];
                memset(slot, 0, sizeof(*slot));
            }

            if (!slot->in_use) {
                // Freshly claimed slot — initialize for this split_id.
                memset(slot, 0, sizeof(*slot));
                slot->split_id     = split_id;
                slot->count        = split_count;
                slot->in_use       = true;
                // Ordering info comes from the first fragment seen — every
                // fragment of one split packet shares the same
                // order_index/order_channel (the sender only allocates
                // one), so capturing it once here is correct.
                slot->is_ordered    = is_ordered;
                slot->order_index   = order_index;
                slot->order_channel = order_channel;
                // Same reasoning applies to reliability/message_index —
                // see the comment on these fields in Session.h.
                slot->is_reliable   = is_reliable;
                slot->message_index = message_index;
                slot->started_ms    = mcp_utils_time_ms();
                s->diag_split_active++;
            }
            slot->last_fragment_ms = mcp_utils_time_ms();

            // Store this fragment
            if (split_index < split_count && !slot->segments[split_index]) {
                slot->segments[split_index] = (uint8_t*)malloc(eplen);
                if (slot->segments[split_index]) {
                    memcpy(slot->segments[split_index], epayload, eplen);
                    slot->seg_sizes[split_index] = eplen;
                    slot->received++;
                }
            }

            // Check if all fragments received
            if (slot->received >= slot->count) {
                // FIX (critical bug from server.log evidence): this is the
                // ONE correct place to apply the reliable-message
                // duplicate/staleness check for a fragmented message — on
                // the reassembled whole, exactly once, matching RakNet's
                // real semantics (one reliableMessageNumber per logical
                // message, assigned before fragmentation). Skipping it
                // here (via has_split above) and only checking here means
                // a genuine retransmit of an entire already-delivered
                // split message is still correctly recognized and
                // dropped — the duplicate check isn't lost, just moved to
                // where the message it actually describes exists as a
                // whole.
                bool duplicate = slot->is_reliable &&
                                  !reliable_index_accept(s, slot->message_index);
                if (!duplicate) {
                    // Calculate total size
                    size_t total = 0;
                    for (uint32_t k = 0; k < slot->count; ++k) {
                        total += slot->seg_sizes[k];
                    }

                    // Concatenate all fragments
                    uint8_t* reassembled = (uint8_t*)malloc(total);
                    if (reassembled) {
                        size_t off = 0;
                        for (uint32_t k = 0; k < slot->count; ++k) {
                            if (slot->segments[k]) {
                                memcpy(reassembled + off, slot->segments[k], slot->seg_sizes[k]);
                                off += slot->seg_sizes[k];
                            }
                        }
                        // Process the complete reassembled packet — through the
                        // same ordering gate as non-fragmented packets, using
                        // the ordering info captured from the first fragment.
                        deliver_ordered(sm, s, slot->is_ordered,
                                         slot->order_index, slot->order_channel,
                                         reassembled, total);
                        free(reassembled);
                    }
                }
                s->diag_split_completed++;

                // Clean up this slot only — every other in-flight split_id
                // for this session is untouched.
                for (uint32_t k = 0; k < slot->count; ++k) {
                    if (slot->segments[k]) { free(slot->segments[k]); slot->segments[k] = NULL; }
                }
                memset(slot, 0, sizeof(*slot));
            }
        } else {
            // Non-fragmented packet — process through the ordering gate.
            deliver_ordered(sm, s, is_ordered, order_index, order_channel,
                             epayload, eplen);
        }

        ptr += consumed;
        rem -= consumed;
    }
}

// =========================================================================
// FIX #13: detailed disconnect log. A bare "session closed" tells us
// nothing about what the reliability layer was actually doing at the
// moment of the kick. This surfaces every counter tracked above so a
// timeout can be correlated with, e.g., a recovery queue that was
// clearly saturated, an ordered channel stuck waiting on a gap, or a
// split reassembly that kept getting evicted — instead of guessing.
// =========================================================================
static void log_session_diagnostics(const mcp_rak_session_t* s, const char* reason) {
    uint64_t now = mcp_utils_time_ms();
    uint32_t split_active_now = 0;
    for (int i = 0; i < MCP_MAX_CONCURRENT_SPLITS; ++i) {
        if (s->split_slots[i].in_use) split_active_now++;
    }
    uint32_t order_hold_active_now = 0;
    for (int c = 0; c < 32; ++c) {
        for (int k = 0; k < MCP_ORDER_HOLD_CAP; ++k) {
            if (s->order_hold[c][k].used) order_hold_active_now++;
        }
    }
    fprintf(stderr,
        "[NET] Session closed\n"
        "  ip=%u.%u.%u.%u:%u\n"
        "  reason=%s\n"
        "  elapsed_ms=%llu\n"
        "  since_last_packet_recv_ms=%llu\n"
        "  since_last_game_packet_recv_ms=%llu\n"
        "  since_last_ping_recv_ms=%llu\n"
        "  since_last_packet_sent_ms=%llu\n"
        "  recv_packets=%llu sent_packets=%llu\n"
        "  ack_received=%llu nack_received=%llu retransmits=%llu\n"
        "  recovery_occupied=%u recovery_peak=%u recovery_overwrites=%u (cap=%u)\n"
        "  split_active_now=%u split_completed=%llu split_expired=%u split_evicted=%u\n"
        "  order_hold_active_now=%u order_hold_peak=%u order_hold_full=%u order_duplicate=%u order_forced_skip=%u\n",
        s->ip[0], s->ip[1], s->ip[2], s->ip[3], s->port,
        reason,
        (unsigned long long)(now - s->start_time_ms),
        (unsigned long long)(now - s->last_packet_received_ms),
        (unsigned long long)(now - s->last_game_packet_received_ms),
        s->last_ping_received_ms ? (unsigned long long)(now - s->last_ping_received_ms) : 0ULL,
        (unsigned long long)(now - s->last_packet_sent_ms),
        (unsigned long long)s->diag_recv_packets, (unsigned long long)s->diag_sent_packets,
        (unsigned long long)s->diag_ack_received, (unsigned long long)s->diag_nack_received,
        (unsigned long long)s->diag_retransmits,
        s->diag_recovery_occupied, s->diag_recovery_peak, s->diag_recovery_overwrites,
        (unsigned)MCP_RECOVERY_CAP,
        split_active_now, (unsigned long long)s->diag_split_completed,
        s->diag_split_expired, s->diag_split_evicted,
        order_hold_active_now, s->diag_order_hold_peak,
        s->diag_order_hold_full, s->diag_order_duplicate, s->diag_order_forced_skip);
}

// =========================================================================
// Handle ACK (0xc0): remove from recovery queue
// =========================================================================
static void handle_ack(mcp_rak_session_t* s,
                         const uint8_t* data, size_t len) {
    if (len < 3) return;
    s->diag_ack_received++;
    size_t i = 1;
    uint16_t count = rb_u16_be(data, len, &i);
    for (uint16_t k = 0; k < count && i < len; ++k) {
        uint8_t record_type = data[i++];
        if (i + 3 > len) break;
        uint32_t lo = rb_u24_le(data, len, &i);
        uint32_t hi = lo;
        if (record_type == 0x00) { // range
            if (i + 3 > len) break;
            hi = rb_u24_le(data, len, &i);
        }
        // SECURITY FIX: a corrupt or malicious ACK can claim an enormous
        // range (e.g. lo=0, hi=0xFFFFFF). Without a cap, the loop below
        // can iterate billions of times, hanging the RakLib thread for
        // seconds — which then starves every session on the server,
        // looking exactly like a timeout/disconnect to players. Real
        // ACK ranges only ever cover recently-sent sequence numbers,
        // which can never span more than the recovery window.
        if (hi < lo || (hi - lo) >= MCP_RECOVERY_CAP) continue;
        for (uint32_t seq = lo; seq <= hi; ++seq) {
            uint32_t slot = seq % MCP_RECOVERY_CAP;
            if (s->recovery[slot].data && s->recovery[slot].seq == seq) {
                free(s->recovery[slot].data);
                s->recovery[slot].data = NULL;
                s->recovery[slot].len  = 0;
                if (s->diag_recovery_occupied > 0) s->diag_recovery_occupied--;
            }
        }
    }
    // NOTE: do NOT update last_activity_ms for ACKs. ACKs are RakNet
    // transport-level confirmations, not game activity.
}
// =========================================================================
// Handle NACK (0xa0): retransmit from recovery
// =========================================================================
static void handle_nack(mcp_rak_session_manager_t* sm, mcp_rak_session_t* s,
                          const uint8_t* data, size_t len) {
    if (len < 3) return;
    s->diag_nack_received++;
    size_t i = 1;
    uint16_t count = rb_u16_be(data, len, &i);
    for (uint16_t k = 0; k < count && i < len; ++k) {
        uint8_t record_type = data[i++];
        if (i + 3 > len) break;
        uint32_t lo = rb_u24_le(data, len, &i);
        uint32_t hi = lo;
        if (record_type == 0x00) {
            if (i + 3 > len) break;
            hi = rb_u24_le(data, len, &i);
        }
        // SECURITY FIX: same unbounded-range hang as handle_ack() above.
        if (hi < lo || (hi - lo) >= MCP_RECOVERY_CAP) continue;
        for (uint32_t seq = lo; seq <= hi; ++seq) {
            uint32_t slot = seq % MCP_RECOVERY_CAP;
            if (s->recovery[slot].data && s->recovery[slot].seq == seq) {
                mcp_rak_session_manager_send(sm,
                    s->recovery[slot].data, s->recovery[slot].len,
                    s->ip, s->port);
            }
        }
    }
    // NOTE: do NOT update last_activity_ms for NACKs. Same as ACKs —
    // this is RakNet transport, not game activity.
}

// =========================================================================
// Handle unconnected (offline) packets
// =========================================================================
static void handle_unconnected(mcp_rak_session_manager_t* sm,
                                  const uint8_t* data, size_t len,
                                  const uint8_t ip[4], uint16_t port) {
    if (len < 1) return;
    uint8_t pid = data[0];
    uint8_t out[512];
    size_t  out_len = 0;

    // ---- UNCONNECTED_PING (0x01) / PING_OPEN_CONNECTIONS (0x02) ----
    if (pid == 0x01 || pid == 0x02) {
        if (len < 17) return;
        size_t pi = 1;
        uint64_t ping_time = rb_u64_le(data, len, &pi);
        out_len = build_unconnected_pong(out, sizeof(out),
                                          ping_time, sm->server_id,
                                          sm->server_name);
        mcp_rak_session_manager_send(sm, out, out_len, ip, port);
        return;
    }

    // ---- OPEN_CONNECTION_REQUEST_1 (0x05) ----
    if (pid == 0x05) {
        uint8_t  protocol;
        uint16_t mtu;
        if (!decode_ocr1(data, len, &protocol, &mtu)) return;

        // Protocol check: MCPE 0.15.10 uses RakNet protocol 6
        // We accept 6 and also 8 (some clients send 8).
        // If wrong protocol we could reject, but PMMP just ignores; we accept.

        // Create/find session
        mcp_rak_session_t* s = mcp_rak_session_manager_get_or_create(sm, ip, port);
        if (!s) return;
        s->mtu   = mtu;
        s->state = MCP_SESSION_STATE_CONNECTING_1;
        s->last_activity_ms = mcp_utils_time_ms();

        out_len = build_ocr1_reply(out, sizeof(out), sm->server_id, mtu);
        mcp_rak_session_manager_send(sm, out, out_len, ip, port);
        return;
    }

    // ---- OPEN_CONNECTION_REQUEST_2 (0x07) ----
    if (pid == 0x07) {
        mcp_rak_session_t* s = mcp_rak_session_manager_find(sm, ip, port);
        if (!s || s->state != MCP_SESSION_STATE_CONNECTING_1) {
            // Can create fresh if state was lost
            s = mcp_rak_session_manager_get_or_create(sm, ip, port);
            if (!s) return;
        }
        uint16_t mtu;
        int64_t  client_id;
        uint16_t srv_port;
        if (!decode_ocr2(data, len, &mtu, &client_id, &srv_port)) return;

        // Clamp MTU like PMMP
        if (mtu > RAKLIB_MAX_MTU) mtu = RAKLIB_MAX_MTU;
        if (mtu < RAKLIB_MIN_MTU) mtu = RAKLIB_MIN_MTU;

        s->mtu       = mtu;
        s->client_id = client_id;
        s->state     = MCP_SESSION_STATE_CONNECTING_2;
        s->last_activity_ms = mcp_utils_time_ms();

        out_len = build_ocr2_reply(out, sizeof(out),
                                    sm->server_id, ip, port, mtu);
        mcp_rak_session_manager_send(sm, out, out_len, ip, port);
        return;
    }

    // ---- Everything else → raw packet callback ----
    if (sm->instance && sm->instance->on_raw_packet) {
        char ipStr[20];
        snprintf(ipStr, sizeof(ipStr), "%u.%u.%u.%u",
                 ip[0], ip[1], ip[2], ip[3]);
        sm->instance->on_raw_packet(sm->instance->impl, ipStr, port, data, len);
    }
}

// =========================================================================
// Flush per-session ACK + NACK
// =========================================================================
static void flush_ack_nack(mcp_rak_session_manager_t* sm,
                             mcp_rak_session_t* s) {
    uint32_t mins[64], maxs[64];

    size_t na = mcp_rak_session_drain_ack_queue(s, mins, maxs, 64);
    if (na > 0) {
        uint8_t ackBuf[512];
        size_t  alen = build_ack_pkt(ackBuf, sizeof(ackBuf), mins, maxs, na, false);
        if (alen > 0)
            mcp_rak_session_manager_send(sm, ackBuf, alen, s->ip, s->port);
    }

    size_t nn = mcp_rak_session_drain_nack_queue(s, mins, maxs, 64);
    if (nn > 0) {
        uint8_t nackBuf[512];
        size_t  nlen = build_ack_pkt(nackBuf, sizeof(nackBuf), mins, maxs, nn, true);
        if (nlen > 0)
            mcp_rak_session_manager_send(sm, nackBuf, nlen, s->ip, s->port);
    }
}

// =========================================================================
// Retransmit recovery queue entries older than 5 seconds
// =========================================================================
static void retransmit_stale(mcp_rak_session_manager_t* sm,
                               mcp_rak_session_t* s) {
    uint64_t now = mcp_utils_time_ms();
    // FIX #7: cap retransmissions per tick. Recovery now holds up to
    // MCP_RECOVERY_CAP (4096) slots per session; if a real network drop
    // leaves a large fraction of them past the 5s staleness threshold at
    // the same moment, retransmitting all of them in one 10ms tick would
    // dump a burst of traffic back onto a connection that just
    // demonstrated it couldn't keep up — making the underlying loss
    // worse instead of recovering from it. Spreading a large backlog
    // across several ticks keeps retransmit traffic proportional instead
    // of bursty.
#define MAX_RETRANSMITS_PER_TICK 32
    size_t sent_this_tick = 0;
    for (size_t k = 0; k < MCP_RECOVERY_CAP && sent_this_tick < MAX_RETRANSMITS_PER_TICK; ++k) {
        if (!s->recovery[k].data) continue;
        if ((now - s->recovery[k].send_time_ms) > 5000) {
            mcp_rak_session_manager_send(sm,
                s->recovery[k].data, s->recovery[k].len,
                s->ip, s->port);
            s->recovery[k].send_time_ms = now;
            s->diag_retransmits++;
            ++sent_this_tick;
        }
    }
}

// =========================================================================
// Main tick
// =========================================================================
void mcp_rak_session_manager_tick(mcp_rak_session_manager_t* sm) {
    if (!sm || sm->socket.fd < 0) return;

    uint8_t  buf[RAKLIB_MAX_MTU + 8];
    uint8_t  ip[4];
    uint16_t port;

    // Reset per-IP flood counter
    sm->ip_sec_n = 0;

    // Receive up to 128 datagrams per tick
    for (int n = 0; n < 128; ++n) {
        ssize_t r = mcp_udp_socket_recv(&sm->socket, buf, sizeof(buf), ip, &port);
        if (r <= 0) break;
        if (r < 1) continue;

        uint8_t pid = buf[0];

        if (pid >= 0x80 && pid <= 0x8F) {
            // Bug 3 fix: use find() instead of get_or_create() for
            // DATA_PACKETs. Connected DATA_PACKETs should only arrive
            // for sessions that already completed the handshake. Using
            // get_or_create would recreate zombie sessions for in-flight
            // retransmits arriving after a client disconnect.
            mcp_rak_session_t* s = mcp_rak_session_manager_find(sm, ip, port);
            if (s) handle_data_packet(sm, s, buf, (size_t)r);
            // else: stale packet from a dead session — ignore silently
        } else if (pid == 0xc0 || pid == 0xc1 || pid == 0xc2 || pid == 0xc3) {
            // ACK
            mcp_rak_session_t* s = mcp_rak_session_manager_find(sm, ip, port);
            if (s) handle_ack(s, buf, (size_t)r);
        } else if (pid == 0xa0 || pid == 0xa1 || pid == 0xa2 || pid == 0xa3) {
            // NACK
            mcp_rak_session_t* s = mcp_rak_session_manager_find(sm, ip, port);
            if (s) handle_nack(sm, s, buf, (size_t)r);
        } else {
            // Offline / unconnected packets
            handle_unconnected(sm, buf, (size_t)r, ip, port);
        }
    }

    // Per-session post-tick: ACK/NACK flush + retransmit + timeout sweep
    uint64_t now = mcp_utils_time_ms();
    for (size_t i = 0; i < sm->session_count; ) {
        mcp_rak_session_t* s = sm->sessions[i];

        flush_ack_nack(sm, s);
        retransmit_stale(sm, s);

        // FIX: evict an incomplete split-packet reassembly buffer that's
        // been waiting more than 10s for a missing fragment. Without this,
        // a permanently-lost fragment (the retransmit itself lost, or the
        // sender gone) left `in_use` set forever, holding every fragment
        // received so far in memory for no reason — this doesn't cause a
        // disconnect by itself, but it's unbounded per-session state that
        // should never persist longer than a couple of RTTs.
        // FIX #3-C: evict order-hold entries stuck waiting for a gap
        // that will never close.
        expire_order_holds(s, now);

        // FIX: evict any incomplete split-packet reassembly slot that's
        // been waiting more than 10s for a missing fragment. Without this,
        // a permanently-lost fragment (the retransmit itself lost, or the
        // sender gone) left a slot occupied forever, holding every
        // fragment received so far in memory for no reason. Only the
        // slot(s) that actually timed out are evicted — every other
        // in-flight split_id for this session is left untouched.
        for (int si = 0; si < MCP_MAX_CONCURRENT_SPLITS; ++si) {
            if (s->split_slots[si].in_use &&
                (now - s->split_slots[si].started_ms) > 10000) {
                for (uint32_t k = 0; k < s->split_slots[si].count && k < MCP_MAX_SPLIT_SEGMENTS; ++k) {
                    if (s->split_slots[si].segments[k]) {
                        free(s->split_slots[si].segments[k]);
                        s->split_slots[si].segments[k] = NULL;
                    }
                }
                memset(&s->split_slots[si], 0, sizeof(s->split_slots[si]));
                s->diag_split_expired++;
            }
        }

        s->is_active = false;

        // Timeout: 30 seconds (matches PMMP default). Only GAME packets
        // reset last_activity_ms (PING/0x00 does NOT reset it). This way,
        // if the client's MCPE app stays alive in Android's background
        // sending PINGs, the server still detects the player is gone
        // after 30s without any real game activity.
        //
        // BUG FIX: Was 15000ms (15s) which was too aggressive for mobile
        // clients that may experience temporary network hiccups or be
        // overwhelmed by chunk data bursts. The workbench showed the
        // client stopped responding after receiving 4 chunks/tick (60+
        // UDP fragments in 3ms) and needed >15s to recover.
        bool should_remove = s->is_disconnecting ||
                             mcp_rak_session_is_timed_out(s, now, 30000);
        if (should_remove) {
            // Only call on_close_session for timeout — NOT for disconnect
            // (the disconnect handler already called it when 0x15 arrived).
            if (!s->is_disconnecting && sm->instance &&
                sm->instance->on_close_session) {
                log_session_diagnostics(s, "TIMEOUT");
                char ipStr[20];
                snprintf(ipStr, sizeof(ipStr), "%u.%u.%u.%u",
                         s->ip[0], s->ip[1], s->ip[2], s->ip[3]);
                sm->instance->on_close_session(sm->instance->impl,
                                                 ipStr, s->port, "timeout");
            }
            mcp_rak_session_destroy(s);
            free(s);
            sm->sessions[i] = sm->sessions[--sm->session_count];
        } else {
            ++i;
        }
    }
}
