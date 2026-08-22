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
                                                uint8_t reliability) {
    if (!sm || !ip || !payload || plen == 0) return false;
    mcp_rak_session_t* s = mcp_rak_session_manager_find(sm, ip, port);
    if (!s || s->state < MCP_SESSION_STATE_CONNECTING_2) return false;
    uint8_t buf[RAKLIB_MAX_MTU + 64];
    size_t n = mcp_rak_session_build_datagram(s, buf, sizeof(buf),
                                               payload, plen, reliability, 0);
    if (n == 0) return false;
    return mcp_rak_session_manager_send(sm, buf, n, ip, port) > 0;
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
// =========================================================================
static size_t parse_encap_record(const uint8_t* buf, size_t buf_len,
                                   const uint8_t** out_payload, size_t* out_plen,
                                   bool* out_done) {
    *out_done = true;
    *out_payload = NULL;
    *out_plen = 0;
    if (buf_len < 3) return 0;

    size_t i = 0;
    uint8_t flags    = buf[i++];
    uint8_t reliability = (flags >> 5) & 0x07;
    bool    has_split   = (flags & 0x10) != 0;

    // bit-length of payload (u16 BE)
    uint16_t bit_len = (uint16_t)((buf[i] << 8) | buf[i+1]); i += 2;
    size_t   plen    = (bit_len + 7) / 8;

    // messageIndex (3-byte LE) for RELIABLE*
    if (reliability == MCP_RELIABILITY_RELIABLE ||
        reliability == MCP_RELIABILITY_RELIABLE_ORDERED ||
        reliability == MCP_RELIABILITY_RELIABLE_SEQUENCED ||
        reliability == MCP_RELIABILITY_RELIABLE_WITH_ACK_RECEIPT ||
        reliability == MCP_RELIABILITY_RELIABLE_ORDERED_WITH_ACK_RECEIPT) {
        if (i + 3 > buf_len) return 0;
        i += 3; // messageIndex
    }

    // sequenceIndex (3-byte LE) for SEQUENCED
    if (reliability == MCP_RELIABILITY_UNRELIABLE_SEQUENCED ||
        reliability == MCP_RELIABILITY_RELIABLE_SEQUENCED) {
        if (i + 3 > buf_len) return 0;
        i += 3;
    }

    // orderIndex (3-byte LE) + orderChannel (1-byte) for ORDERED
    if (reliability == MCP_RELIABILITY_RELIABLE_ORDERED ||
        reliability == MCP_RELIABILITY_RELIABLE_ORDERED_WITH_ACK_RECEIPT) {
        if (i + 4 > buf_len) return 0;
        i += 4;
    }

    // split info
    if (has_split) {
        if (i + 10 > buf_len) return 0;
        i += 4; // splitCount u32 BE
        i += 2; // splitID    u16 BE
        i += 4; // splitIndex u32 BE
    }

    if (i + plen > buf_len) return 0;
    *out_payload = buf + i;
    *out_plen    = plen;
    *out_done    = false;
    return i + plen;
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
            // Wrap in DataPacket_4 (0x84) with UNRELIABLE
            uint8_t dp[RAKLIB_MAX_MTU + 64];
            size_t  dp_len = mcp_rak_session_build_datagram(s, dp, sizeof(dp),
                                dgram, dgram_len, MCP_RELIABILITY_UNRELIABLE, 0);
            if (dp_len > 0)
                mcp_rak_session_manager_send(sm, dp, dp_len, s->ip, s->port);
            return;
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
            return;
        }
    }

    if (s->state == MCP_SESSION_STATE_CONNECTED) {
        // ---- PING (0x00) — internal keep-alive ----
        if (pid == 0x00 && plen >= 9) {
            size_t i = 1;
            int64_t ping_id = (int64_t)rb_u64_be(payload, plen, &i);
            // Reply: PONG (0x03)
            size_t oi = 0;
            dgram[oi++] = 0x03;
            write_i64_be(dgram, &oi, ping_id);
            dgram_len = oi;
            uint8_t dp[64];
            size_t dp_len = mcp_rak_session_build_datagram(s, dp, sizeof(dp),
                                dgram, dgram_len, MCP_RELIABILITY_UNRELIABLE, 0);
            if (dp_len > 0)
                mcp_rak_session_manager_send(sm, dp, dp_len, s->ip, s->port);
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

    // Sequence-number / window logic (mirrors PMMP Session.php)
    if ((int32_t)seq < (int32_t)s->window_start ||
        seq >= s->window_end) {
        return; // out of window — drop
    }
    uint32_t slot = seq % MCP_SESSION_WINDOW_SIZE;
    if (s->recv_window[slot]) return; // duplicate

    int32_t diff = (int32_t)seq - s->last_recv_seq;
    mcp_rak_session_queue_ack(s, seq);
    s->recv_window[slot] = true;

    if (diff != 1) {
        // Gap: queue NACKs for missing sequences
        int32_t from = s->last_recv_seq + 1;
        int32_t to   = (int32_t)seq;
        for (int32_t m = from; m < to; ++m) {
            uint32_t wslot = (uint32_t)m % MCP_SESSION_WINDOW_SIZE;
            if (!s->recv_window[wslot])
                mcp_rak_session_queue_nack(s, (uint32_t)m);
        }
    }
    if (diff >= 1) {
        s->last_recv_seq = (int32_t)seq;
        s->window_start  = (uint32_t)((int32_t)seq - MCP_SESSION_WINDOW_SIZE / 2);
        s->window_end    = (uint32_t)(seq + MCP_SESSION_WINDOW_SIZE / 2);
    }
    s->last_activity_ms = mcp_utils_time_ms();
    s->is_active = true;

    // Parse encapsulated records
    const uint8_t* ptr = data + payload_off;
    size_t         rem = len  - payload_off;
    while (rem > 0) {
        const uint8_t* epayload;
        size_t eplen;
        bool   done;
        size_t consumed = parse_encap_record(ptr, rem, &epayload, &eplen, &done);
        if (consumed == 0 || done) break;
        handle_encapsulated(sm, s, epayload, eplen);
        ptr += consumed;
        rem -= consumed;
    }
}

// =========================================================================
// Handle ACK (0xc0): remove from recovery queue
// =========================================================================
static void handle_ack(mcp_rak_session_t* s,
                         const uint8_t* data, size_t len) {
    if (len < 3) return;
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
        for (uint32_t seq = lo; seq <= hi; ++seq) {
            uint32_t slot = seq % MCP_RECOVERY_CAP;
            if (s->recovery[slot].data && s->recovery[slot].seq == seq) {
                free(s->recovery[slot].data);
                s->recovery[slot].data = NULL;
                s->recovery[slot].len  = 0;
            }
        }
    }
    s->last_activity_ms = mcp_utils_time_ms();
    s->is_active = true;
}

// =========================================================================
// Handle NACK (0xa0): retransmit from recovery
// =========================================================================
static void handle_nack(mcp_rak_session_manager_t* sm, mcp_rak_session_t* s,
                          const uint8_t* data, size_t len) {
    if (len < 3) return;
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
        for (uint32_t seq = lo; seq <= hi; ++seq) {
            uint32_t slot = seq % MCP_RECOVERY_CAP;
            if (s->recovery[slot].data && s->recovery[slot].seq == seq) {
                mcp_rak_session_manager_send(sm,
                    s->recovery[slot].data, s->recovery[slot].len,
                    s->ip, s->port);
            }
        }
    }
    s->last_activity_ms = mcp_utils_time_ms();
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
// Retransmit recovery queue entries older than 8 seconds
// =========================================================================
static void retransmit_stale(mcp_rak_session_manager_t* sm,
                               mcp_rak_session_t* s) {
    uint64_t now = mcp_utils_time_ms();
    for (size_t k = 0; k < MCP_RECOVERY_CAP; ++k) {
        if (!s->recovery[k].data) continue;
        if ((now - s->recovery[k].send_time_ms) > 8000) {
            mcp_rak_session_manager_send(sm,
                s->recovery[k].data, s->recovery[k].len,
                s->ip, s->port);
            s->recovery[k].send_time_ms = now;
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
            // Connected DATA_PACKET — needs a session
            mcp_rak_session_t* s = mcp_rak_session_manager_get_or_create(sm, ip, port);
            if (s) handle_data_packet(sm, s, buf, (size_t)r);
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
        s->is_active = false;

        // Timeout: 10 seconds
        if (mcp_rak_session_is_timed_out(s, now, 10000)) {
            if (sm->instance && sm->instance->on_close_session) {
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
