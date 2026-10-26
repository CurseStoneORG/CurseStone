/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

 * CurseStone | Open Source Server Software
 *
 * Copyright (C) 2026 CurseStoneTeam
 *
 * Developed by CurseStoneTeam.
 * Built for an open and extensible Minecraft server ecosystem.
 *
 * Licensed under the GNU Lesser General Public License v3.0 or later.
 * You may study, modify, and redistribute this software under its license.
 *
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 * Repository: https://github.com/CurseStoneTeam/CurseStone
 *
 */
// SPDX-License-Identifier: LGPL-3.0-or-later
// SessionManager.c — Full RakNet handshake + dispatch loop. Pure C.
// Ported from raklib/server/Session + SessionManager
// (the reference server 0.15.x / Protocol 84 compatible)
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
#include "../../utils/DebugFlag.h"
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
static const uint8_t RAKLIB_MAGIC_BYTES[16] = {0x00, 0xff, 0xff, 0x00, 0xfe, 0xfe, 0xfe, 0xfe,
                                               0xfd, 0xfd, 0xfd, 0xfd, 0x12, 0x34, 0x56, 0x78};

// =========================================================================
// Init / Destroy
// =========================================================================
void mcp_rak_session_manager_init(mcp_rak_session_manager_t* sm, int64_t server_id, uint16_t mtu,
                                  mcp_server_instance_t* instance) {
    if (!sm)
        return;
    memset(sm, 0, sizeof(*sm));
    sm->socket.fd = -1;
    sm->server_id = server_id;
    sm->mtu = mtu ? mtu : RAKLIB_MAX_MTU;
    sm->instance = instance;
    snprintf(sm->server_name, sizeof(sm->server_name), "MCCPP;Demo;84;0.15.10;0;20");
}

void mcp_rak_session_manager_destroy(mcp_rak_session_manager_t* sm) {
    if (!sm)
        return;
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

void mcp_rak_session_manager_set_name(mcp_rak_session_manager_t* sm, const char* name) {
    if (!sm || !name)
        return;
    snprintf(sm->server_name, sizeof(sm->server_name), "%s", name);
}

// =========================================================================
// Bind
// =========================================================================
int mcp_rak_session_manager_bind(mcp_rak_session_manager_t* sm, const char* host, uint16_t port) {
    if (!sm)
        return -1;
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
mcp_rak_session_t* mcp_rak_session_manager_find(mcp_rak_session_manager_t* sm, const uint8_t ip[4],
                                                uint16_t port) {
    for (size_t i = 0; i < sm->session_count; ++i) {
        if (memcmp(sm->sessions[i]->ip, ip, 4) == 0 && sm->sessions[i]->port == port)
            return sm->sessions[i];
    }
    return NULL;
}

mcp_rak_session_t* mcp_rak_session_manager_get_or_create(mcp_rak_session_manager_t* sm,
                                                         const uint8_t ip[4], uint16_t port) {
    mcp_rak_session_t* s = mcp_rak_session_manager_find(sm, ip, port);
    if (s)
        return s;
    if (sm->session_count >= MCP_SM_MAX_SESSIONS)
        return NULL;
    if (sm->session_count == sm->session_capacity) {
        size_t nc = sm->session_capacity == 0 ? 8 : sm->session_capacity * 2;
        mcp_rak_session_t** arr =
            (mcp_rak_session_t**)realloc(sm->sessions, nc * sizeof(mcp_rak_session_t*));
        if (!arr)
            return NULL;
        sm->sessions = arr;
        sm->session_capacity = nc;
    }
    mcp_rak_session_t* ns = (mcp_rak_session_t*)calloc(1, sizeof(*ns));
    if (!ns)
        return NULL;
    mcp_rak_session_init(ns, ip, port, 0, sm->server_id);
    sm->sessions[sm->session_count++] = ns;
    return ns;
}

// =========================================================================
// Send helpers
// =========================================================================
ssize_t mcp_rak_session_manager_send(mcp_rak_session_manager_t* sm, const uint8_t* data, size_t len,
                                     const uint8_t ip[4], uint16_t port) {
    if (!sm || !data || !ip)
        return -1;
    return mcp_udp_socket_send(&sm->socket, data, len, ip, port);
}

// ---- Congestion backlog (see the long comment on `backlog` in
// Session.h). Push/flush run only on the RakLib thread.
static bool session_backlog_push(mcp_rak_session_t* s, const uint8_t* payload, size_t plen,
                                 uint8_t reliability, uint8_t order_channel) {
    if (!s || !payload || plen == 0)
        return false;
    if (s->backlog_count >= MCP_BACKLOG_MAX_ENTRIES)
        return false;
    if (s->backlog_bytes + plen > MCP_BACKLOG_MAX_BYTES)
        return false;
    uint8_t* copy = (uint8_t*)malloc(plen);
    if (!copy)
        return false;
    memcpy(copy, payload, plen);
    uint32_t idx = s->backlog_tail;
    s->backlog[idx].data = copy;
    s->backlog[idx].len = plen;
    s->backlog[idx].reliability = reliability;
    s->backlog[idx].order_channel = order_channel;
    s->backlog_tail = (idx + 1) % MCP_BACKLOG_MAX_ENTRIES;
    s->backlog_count++;
    s->backlog_bytes += plen;
    s->diag_backlog_queued++;
    if (s->backlog_count > s->diag_backlog_peak)
        s->diag_backlog_peak = s->backlog_count;
    if (s->backlog_bytes > s->diag_backlog_bytes_peak)
        s->diag_backlog_bytes_peak = s->backlog_bytes;
    return true;
}

// Replays queued payloads (oldest first) while the session has recovered
// enough capacity. Called after ACK handling and once per RakLib tick.
static void session_backlog_flush(mcp_rak_session_manager_t* sm, mcp_rak_session_t* s) {
    if (!sm || !s || s->backlog_count == 0)
        return;
    if (s->state < MCP_SESSION_STATE_CONNECTING_2)
        return;
    while (s->backlog_count > 0 &&
           s->diag_recovery_occupied < MCP_BACKLOG_FLUSH_WATERMARK) {
        uint32_t idx = s->backlog_head;
        uint8_t* data = s->backlog[idx].data;
        size_t len = s->backlog[idx].len;
        uint8_t reliability = s->backlog[idx].reliability;
        uint8_t channel = s->backlog[idx].order_channel;
        s->backlog[idx].data = NULL;
        s->backlog_head = (idx + 1) % MCP_BACKLOG_MAX_ENTRIES;
        s->backlog_count--;
        s->backlog_bytes -= len;
        if (data) {
            // Replay through the normal send path so the payload is
            // fragmented, message-indexed and recovery-tracked exactly
            // like a fresh send. A false return here means the datagrams
            // could not go out (UDP failure / dead session) — dropping
            // the entry matches what a direct send would have done.
            if (mcp_rak_session_send_payload(s, sm, data, len, reliability, channel))
                s->diag_backlog_flushed++;
            free(data);
        }
    }
}

bool mcp_rak_session_manager_send_encapsulated(mcp_rak_session_manager_t* sm, const uint8_t ip[4],
                                               uint16_t port, const uint8_t* payload, size_t plen,
                                               uint8_t reliability, uint8_t order_channel) {
    if (!sm || !ip || !payload || plen == 0)
        return false;
    mcp_rak_session_t* s = mcp_rak_session_manager_find(sm, ip, port);
    if (!s || s->state < MCP_SESSION_STATE_CONNECTING_2)
        return false;
    // ---- Flow control: never let one stalled session grow without end.
    // A client that has stopped acknowledging (crashed, saturated phone,
    // proxy that stopped forwarding) used to keep the server queueing
    // MORE reliable data and resending everything it had, at a fixed 5s
    // cadence, until the 30s timeout: the field dump showed
    // recovery_occupied=468 with retransmits=5136 for one such client.
    // Past the ceiling this session is already lost, so refusing the new
    // data keeps memory, CPU and the shared network thread free for the
    // players that are actually still there (a healthy client sits in the
    // tens, so this never fires in normal play).
    if (s->diag_recovery_occupied >= MCP_MAX_INFLIGHT_RELIABLE) {
        // Congestion backlog first: a healthy-but-slow client (phone over
        // Wi-Fi/mobile data during heavy chunk streaming) only needs the
        // payload DELAYED, not lost. Only when the backlog itself is full
        // is the session genuinely not draining — drop and count it.
        if (session_backlog_push(s, payload, plen, reliability, order_channel))
            return true; // accepted, deferred
        s->diag_backpressure_drops++;
        if (mcp_debug_enabled() && !s->diag_backpressure_logged) {
            s->diag_backpressure_logged = true;
            fprintf(stderr,
                    "[NET] %u.%u.%u.%u:%u is not acknowledging: %u reliable "
                    "datagrams still un-ACKed (cap %u) — refusing new reliable "
                    "data for this session (drops=%u)\n",
                    s->ip[0], s->ip[1], s->ip[2], s->ip[3], s->port, s->diag_recovery_occupied,
                    (unsigned)MCP_MAX_INFLIGHT_RELIABLE, s->diag_backpressure_drops);
        }
        return false;
    }
    // The session timeout is a RECEIVE clock: last_activity_ms is only
    // stamped by datagrams/ACKs/NACKs arriving FROM the client (and by
    // the handshake), never by our own sends. Stamping it here used to
    // keep a half-dead session alive FOREVER: the client stops sending,
    // but the server keeps broadcasting world updates (other players'
    // movement, block sync, entity events) to it, and every one of those
    // outbound sends reset the 30s sweep — the exact "player frozen, not
    // moving, still takes damage, other players still see the ghost"
    // report. last_packet_sent_ms stays send-stamped because it drives
    // the keepalive PING cadence and the health line.
    s->last_packet_sent_ms = mcp_utils_time_ms();
    s->diag_sent_packets++;
    return mcp_rak_session_send_payload(s, sm, payload, plen, reliability, order_channel);
}

// =========================================================================
// FIX (the Magmanode/"timeout without ever opening a session" diagnosis):
// every handshake-phase reply (UNCONNECTED_PONG, OPEN_CONNECTION_REPLY_1/2)
// used to go straight to the raw socket send, bypassing diag_sent_packets
// and last_packet_sent_ms. A session that died during the handshake printed
// the misleading "sent_packets=0 since_last_packet_sent_ms=<full session>"
// seen in real disconnect dumps — even though replies HAD gone out — making
// it impossible to tell "the server never replied" (code bug) from "the
// server replied, the network lost it" (hosting panel / firewall / lossy
// mobile-CGNAT path). This wrapper counts successful sends, stamps the sent
// clock, and logs the FIRST send failure per session, so a host whose
// outbound UDP is blocked shows one explicit WARNING in the console instead
// of silently looking like a client that vanished into thin air.
// `s` may be NULL for pre-session traffic (server-list pings) — such sends
// are not counted (no session to count on) and never spam failure logs.
// =========================================================================
static ssize_t sm_send_reply(mcp_rak_session_manager_t* sm, mcp_rak_session_t* s,
                             const uint8_t* data, size_t len, const uint8_t ip[4], uint16_t port) {
    if (!sm || !data || !ip || len == 0)
        return -1;
    ssize_t r = mcp_rak_session_manager_send(sm, data, len, ip, port);
    if (r > 0) {
        if (s) {
            s->diag_sent_packets++;
            s->last_packet_sent_ms = mcp_utils_time_ms();
        }
    } else if (r < 0 && s && !s->diag_send_fail_logged) {
        fprintf(stderr,
                "[NET] WARNING: UDP send to %u.%u.%u.%u:%u FAILED (result=%zd). "
                "Outbound UDP may be blocked by the host firewall/panel — "
                "clients will time out during the handshake.\n",
                ip[0], ip[1], ip[2], ip[3], port, (ssize_t)r);
        s->diag_send_fail_logged = true;
    }
    return r;
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
    for (int s = 56; s >= 0; s -= 8)
        b[(*oi)++] = (uint8_t)(v >> s);
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
static void write_address(uint8_t* b, size_t* oi, const uint8_t ip[4], uint16_t port) {
    write_u8(b, oi, 4); // IPv4
    write_bytes(b, oi, ip, 4);
    write_u16_be(b, oi, port);
}
// RakLib-style string: [u16_be length][bytes]
static void write_rakstring(uint8_t* b, size_t* oi, const char* str) {
    uint16_t len = str ? (uint16_t)strlen(str) : 0;
    write_u16_be(b, oi, len);
    if (len)
        write_bytes(b, oi, str, len);
}

// Read helpers
static uint8_t rb_u8(const uint8_t* b, size_t len, size_t* i) {
    if (*i >= len)
        return 0;
    return b[(*i)++];
}
static uint16_t rb_u16_be(const uint8_t* b, size_t len, size_t* i) {
    if (*i + 2 > len)
        return 0;
    uint16_t v = ((uint16_t)b[*i] << 8) | b[*i + 1];
    *i += 2;
    return v;
}
static uint32_t rb_u24_le(const uint8_t* b, size_t len, size_t* i) {
    if (*i + 3 > len)
        return 0;
    uint32_t v = b[*i] | ((uint32_t)b[*i + 1] << 8) | ((uint32_t)b[*i + 2] << 16);
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
#define MCP_SEQ24_HALF 0x00800000u

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
    if (d >= MCP_SEQ24_HALF)
        d -= (MCP_SEQ24_MASK + 1);
    return (int32_t)d;
}

static uint64_t rb_u64_be(const uint8_t* b, size_t len, size_t* i) {
    if (*i + 8 > len)
        return 0;
    uint64_t v = 0;
    for (int s = 56; s >= 0; s -= 8)
        v |= ((uint64_t)b[(*i)++] << s);
    return v;
}
static uint64_t rb_u64_le(const uint8_t* b, size_t len, size_t* i) {
    if (*i + 8 > len)
        return 0;
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
static size_t build_ack_pkt(uint8_t* out, size_t cap, const uint32_t* mins, const uint32_t* maxs,
                            size_t n, bool is_nack) {
    size_t oi = 0;
    if (oi + 3 + n * 7 > cap)
        return 0;
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
// The reference server writes ping_time as LE (getLong = LE), server_id as BE (getLong = BE
// only for server fields). Mirror that exactly.
// =========================================================================
static size_t build_unconnected_pong(uint8_t* out, size_t cap, uint64_t ping_time,
                                     int64_t server_id, const char* name) {
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
static size_t build_ocr1_reply(uint8_t* out, size_t cap, int64_t server_id, uint16_t mtu) {
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
static size_t build_ocr2_reply(uint8_t* out, size_t cap, int64_t server_id,
                               const uint8_t client_ip[4], uint16_t client_port, uint16_t mtu) {
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
static size_t build_server_handshake(uint8_t* out, size_t cap, const uint8_t client_ip[4],
                                     uint16_t client_port, int64_t send_ping) {
    static const uint8_t null_ip[4] = {0, 0, 0, 0};
    size_t oi = 0;
    out[oi++] = 0x10;
    write_address(out, &oi, client_ip, client_port);
    write_u16_be(out, &oi, 0); // short = 0 (unknown field, the reference server writes 0)
    // 10 system addresses (127.0.0.1:0 + 9x 0.0.0.0:0)
    static const uint8_t loopback[4] = {127, 0, 0, 1};
    write_address(out, &oi, loopback, 0);
    for (int i = 1; i < 10; ++i)
        write_address(out, &oi, null_ip, 0);
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
// Log a handshake-stage packet that was refused or could not be parsed.
//
// Handshake packets (OCR1/OCR2/CLIENT_CONNECT/CLIENT_HANDSHAKE) arrive a
// handful of times per connection, so logging them unconditionally is
// safe. This exists because those packets used to be dropped with a bare
// `return`: a client whose handshake the server could not parse produced
// a completely EMPTY log — the server never created a session and never
// said why, which is exactly the "it just says the session closed and
// nothing else" symptom. Data packets are NOT logged here (far too many).
static void note_handshake_drop(const uint8_t* data, size_t len, const uint8_t ip[4], uint16_t port,
                                const char* why) {
    char hex[80];
    size_t cap = len < 16 ? len : 16;
    size_t o = 0;
    hex[0] = '\0';
    for (size_t i = 0; i < cap && o + 4 < sizeof(hex); ++i)
        o += (size_t)snprintf(hex + o, sizeof(hex) - o, "%02x ", data[i]);
    if (mcp_debug_enabled()) {
    fprintf(stderr,
            "[NET] DROPPED 0x%02x packet from %u.%u.%u.%u:%u (%zu bytes)"
            " -- %s | first bytes: %s\n",
            data[0], ip[0], ip[1], ip[2], ip[3], port, len, why, hex);
    }
}

static bool decode_ocr1(const uint8_t* data, size_t len, uint8_t* out_protocol, uint16_t* out_mtu) {
    if (len < 18)
        return false; // 1(id) + 16(magic) + 1(protocol) min
    // skip id(1) + magic(16)
    *out_protocol = data[17];
    // Remaining bytes after protocol are padding to probe MTU
    // MTU = len + 28 (IP header + UDP header not in payload)
    *out_mtu = (uint16_t)(len + 28);
    if (*out_mtu > RAKLIB_MAX_MTU)
        *out_mtu = RAKLIB_MAX_MTU;
    return true;
}

// =========================================================================
// Decode OPEN_CONNECTION_REQUEST_2 (0x07)
// Layout: [id][magic 16][serverAddress][mtu u16 BE][clientID i64]
// =========================================================================
static bool decode_ocr2(const uint8_t* data, size_t len, uint16_t* out_mtu, int64_t* out_client_id,
                        uint16_t* out_server_port) {
    // Layout: [id][magic 16][serverAddress][mtu u16 BE][clientID i64]
    //
    // RakNet's serverAddress is 7 bytes: [version u8][ip 4][port 2],
    // making a conforming packet 34 bytes. Several clients in the wild
    // (e.g. the LegacyBot stress tester) omit the version byte and write
    // only 6, so the whole packet arrives 33 bytes long. The old code
    // demanded `len >= 34` and dropped those packets SILENTLY, so such a
    // client never received OPEN_CONNECTION_REPLY_2 and just sat there
    // timing out. Accept both forms: 7-byte address whenever the packet
    // is long enough for the canonical layout, 6-byte otherwise. Every
    // read is bounds-checked, and the trailing clientID is optional
    // (older clients omit it) rather than a hard failure.
    if (len < 1 + 16 + 6 + 2)
        return false;
    size_t i = 1 + 16; // skip id + magic
    size_t addr = (len >= 1 + 16 + 7 + 2 + 8) ? 7 : 6;
    if (len < i + addr + 2)
        return false;
    i += addr - 2; // land on the port field
    *out_server_port = (uint16_t)((data[i] << 8) | data[i + 1]);
    i += 2;
    *out_mtu = (uint16_t)((data[i] << 8) | data[i + 1]);
    i += 2;
    if (*out_mtu > RAKLIB_MAX_MTU)
        *out_mtu = RAKLIB_MAX_MTU;
    *out_client_id = 0;
    if (i + 8 <= len)
        *out_client_id = rb_i64_be(data, len, &i);
    return true;
}

// =========================================================================
// Decode RakNet DATA_PACKET (0x80..0x8F) header
// Returns payload offset, fills seq.
// =========================================================================
static bool decode_data_header(const uint8_t* data, size_t len, uint32_t* out_seq,
                               size_t* out_payload_off) {
    if (len < 4)
        return false;
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
static size_t parse_encap_record(const uint8_t* buf, size_t buf_len, const uint8_t** out_payload,
                                 size_t* out_plen, bool* out_done, bool* out_has_split,
                                 uint32_t* out_split_count, uint16_t* out_split_id,
                                 uint32_t* out_split_index, uint8_t* out_reliability,
                                 bool* out_is_reliable, uint32_t* out_message_index,
                                 bool* out_is_ordered, uint32_t* out_order_index,
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
    if (buf_len < 3)
        return 0;

    size_t i = 0;
    uint8_t flags = buf[i++];
    uint8_t reliability = (flags >> 5) & 0x07;
    bool has_split = (flags & 0x10) != 0;
    *out_reliability = reliability;

    // bit-length of payload (u16 BE)
    uint16_t bit_len = (uint16_t)((buf[i] << 8) | buf[i + 1]);
    i += 2;
    size_t plen = (bit_len + 7) / 8;

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
    bool is_ordered = (reliability == MCP_RELIABILITY_RELIABLE_ORDERED ||
                       reliability == MCP_RELIABILITY_RELIABLE_ORDERED_WITH_ACK_RECEIPT);
    bool is_sequenced = (reliability == MCP_RELIABILITY_UNRELIABLE_SEQUENCED ||
                         reliability == MCP_RELIABILITY_RELIABLE_SEQUENCED);
    *out_is_reliable = is_reliable;

    // messageIndex (3-byte LE) — present for every is_reliable value above.
    if (is_reliable) {
        if (i + 3 > buf_len)
            return 0;
        // FIX #2: actually decode this instead of just skipping past it.
        // This is the reliable MESSAGE index — distinct from both the
        // datagram sequence number and the ordering index. Needed to
        // give reliable messages their own duplicate/staleness window,
        // independent of which physical datagram (and possibly which
        // retransmit) carried them.
        uint32_t mi = (uint32_t)buf[i] | ((uint32_t)buf[i + 1] << 8) | ((uint32_t)buf[i + 2] << 16);
        i += 3;
        *out_message_index = mi;
    }

    // sequenceIndex (3-byte LE) for SEQUENCED
    if (is_sequenced) {
        if (i + 3 > buf_len)
            return 0;
        i += 3;
    }

    // orderIndex (3-byte LE) + orderChannel (1-byte) for ORDERED
    if (is_ordered) {
        if (i + 4 > buf_len)
            return 0;
        // FIX: actually decode these instead of just skipping past them.
        // Without this, the caller has no way to know a packet needs to
        // wait for earlier packets on its channel.
        uint32_t oi = (uint32_t)buf[i] | ((uint32_t)buf[i + 1] << 8) | ((uint32_t)buf[i + 2] << 16);
        i += 3;
        uint8_t chan = buf[i++] & 0x1F;
        *out_is_ordered = true;
        *out_order_index = oi;
        *out_order_channel = chan;
    }

    // split info
    if (has_split) {
        if (i + 10 > buf_len)
            return 0;
        *out_has_split = true;
        *out_split_count = ((uint32_t)buf[i] << 24) | ((uint32_t)buf[i + 1] << 16) |
                           ((uint32_t)buf[i + 2] << 8) | buf[i + 3];
        i += 4;
        *out_split_id = ((uint16_t)buf[i] << 8) | buf[i + 1];
        i += 2;
        *out_split_index = ((uint32_t)buf[i] << 24) | ((uint32_t)buf[i + 1] << 16) |
                           ((uint32_t)buf[i + 2] << 8) | buf[i + 3];
        i += 4;
    }

    if (i + plen > buf_len)
        return 0;
    *out_payload = buf + i;
    *out_plen = plen;
    *out_done = false;
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
//
// `payload`/`plen` are the bytes of the message being delivered. They are
// used ONLY to tell a true retransmit (same index, same bytes -> drop)
// apart from a client that reuses one index for several distinct messages
// (same index, different bytes -> accept). See reliable_seen[] in
// Session.h for why that distinction matters.
// =========================================================================
static uint32_t reliable_payload_hash(const uint8_t* p, size_t n) {
    // FNV-1a, 32-bit.
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; ++i) {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}

static void reliable_remember(mcp_rak_session_t* s, uint32_t message_index, const uint8_t* payload,
                              size_t plen) {
    // Direct-mapped by the SAME modulus reliable_recv_window uses, so the
    // two structures describe exactly the same index space and can never
    // disagree about whether a given index is still "in the window".
    uint32_t slot = message_index % MCP_RELIABLE_WINDOW_SIZE;
    s->reliable_seen[slot].index = message_index;
    s->reliable_seen[slot].hash = reliable_payload_hash(payload, plen);
    s->reliable_seen[slot].len = (uint32_t)plen;
    s->reliable_seen[slot].used = true;
}

// True when this exact (index, bytes) pair was already delivered.
static bool reliable_already_delivered(mcp_rak_session_t* s, uint32_t message_index,
                                       const uint8_t* payload, size_t plen) {
    if (!payload)
        return false;
    // O(1) direct lookup — the entry for this index can only ever live at
    // message_index % MCP_RELIABLE_WINDOW_SIZE, so there is no ring to scan
    // and no 256-entry blind spot (see the struct's comment in Session.h).
    uint32_t slot = message_index % MCP_RELIABLE_WINDOW_SIZE;
    if (!s->reliable_seen[slot].used)
        return false;
    if (s->reliable_seen[slot].index != message_index)
        return false;
    if (s->reliable_seen[slot].len != (uint32_t)plen)
        return false;
    return s->reliable_seen[slot].hash == reliable_payload_hash(payload, plen);
}

static bool reliable_index_accept(mcp_rak_session_t* s, uint32_t message_index,
                                  const uint8_t* payload, size_t plen) {
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
        if (s->reliable_recv_window[slot]) {
            // Index already used. Only treat it as a duplicate when these
            // are the very same bytes (a retransmit). A client that reuses
            // one index for DIFFERENT messages is non-conforming, but
            // dropping its traffic silently strands the session before it
            // ever logs in, so let it through instead.
            if (reliable_already_delivered(s, message_index, payload, plen)) {
                // A genuine retransmit (same index AND same bytes) — the
                // game layer already saw this message, so dropping it is
                // correct. Log the FIRST one only; a lossy link can
                // retransmit constantly and one stderr line per duplicate
                // would drown the console. The running total is surfaced in
                // the disconnect dump instead.
                s->diag_duplicate_dropped++;
                if (mcp_debug_enabled() && !s->diag_duplicate_logged) {
                    s->diag_duplicate_logged = true;
                    fprintf(stderr,
                            "[NET] Duplicate reliable message dropped "
                            "(index=%u, %zu bytes) from %u.%u.%u.%u:%u "
                            "(suppressing further such lines this session)\n",
                            message_index, plen, s->ip[0], s->ip[1], s->ip[2], s->ip[3], s->port);
                }
                return false;
            }
            reliable_remember(s, message_index, payload, plen);
            return true;
        }
        s->reliable_recv_window[slot] = true;
        reliable_remember(s, message_index, payload, plen);
        if (seq24_greater(message_index, (uint32_t)s->last_reliable_index)) {
            s->last_reliable_index = (int32_t)message_index;
        }
        // Slide the window forward if we've accepted enough of the front
        // half — mirrors the same slide strategy already used for the
        // transport-level recv_window below.
        // FIX: circular comparison — a plain >= here has the same
        // wrap hazard as everywhere else message_index is compared.
        if (seq24_diff(message_index, s->reliable_window_start) >=
            (int32_t)(MCP_RELIABLE_WINDOW_SIZE / 2)) {
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
static void handle_encapsulated(mcp_rak_session_manager_t* sm, mcp_rak_session_t* s,
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
// True when this exact (channel, order_index, bytes) was already delivered.
static bool order_already_delivered(mcp_rak_session_t* s, uint8_t chan, uint32_t order_index,
                                    const uint8_t* payload, size_t plen) {
    if (!payload)
        return false;
    uint32_t h = reliable_payload_hash(payload, plen);
    for (uint32_t i = 0; i < MCP_ORDER_DELIVERED_SLOTS; ++i) {
        if (!s->order_delivered[i].used)
            continue;
        if (s->order_delivered[i].chan != chan)
            continue;
        if (s->order_delivered[i].index != order_index)
            continue;
        if (s->order_delivered[i].hash == h && s->order_delivered[i].len == plen)
            return true;
    }
    return false;
}

static void order_remember_delivered(mcp_rak_session_t* s, uint8_t chan, uint32_t order_index,
                                     const uint8_t* payload, size_t plen) {
    uint32_t i = s->order_delivered_next % MCP_ORDER_DELIVERED_SLOTS;
    s->order_delivered[i].chan = chan;
    s->order_delivered[i].index = order_index;
    s->order_delivered[i].hash = payload ? reliable_payload_hash(payload, plen) : 0;
    s->order_delivered[i].len = (uint32_t)plen;
    s->order_delivered[i].used = true;
    s->order_delivered_next = i + 1;
}

static void deliver_ordered(mcp_rak_session_manager_t* sm, mcp_rak_session_t* s, bool is_ordered,
                            uint32_t order_index, uint8_t order_channel, const uint8_t* payload,
                            size_t plen) {
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
            // Normally a stale index = retransmit of something already
            // delivered, so drop it. Compare the bytes before believing
            // that: a client that keeps reusing ONE order index for every
            // packet (some stress testers do) would otherwise have all of
            // its traffic after the first packet discarded here, leaving
            // the session half-open with an empty log. Identical bytes =
            // real retransmit; different bytes = a new message wearing an
            // old index, so deliver it.
            if (order_already_delivered(s, chan, order_index, payload, plen)) {
                s->diag_order_duplicate++;
                return;
            }
            if (mcp_debug_enabled() && !s->diag_order_index_reuse_logged) {
                s->diag_order_index_reuse_logged = true;
                fprintf(stderr,
                        "[NET] WARNING: %u.%u.%u.%u:%u reuses ordered-channel "
                        "order indexes (got %u, expected %u). Delivering it "
                        "anyway; packet reordering protection is degraded for "
                        "this session.\n",
                        s->ip[0], s->ip[1], s->ip[2], s->ip[3], s->port, order_index,
                        s->recv_order_index[chan]);
            }
            s->diag_order_index_reuse++;
            order_remember_delivered(s, chan, order_index, payload, plen);
            // Re-anchor the channel on this index so the next packet is
            // compared against what we just accepted.
            s->recv_order_index[chan] = (order_index + 1) & MCP_SEQ24_MASK;
            s->recv_order_advanced_ms[chan] = mcp_utils_time_ms();
            handle_encapsulated(sm, s, payload, plen);
            return;
        }
        // FIX #3-A: reject if this exact order_index is already sitting
        // in the hold buffer. Without this check, a retransmit of a
        // still-pending ordered packet would claim a SECOND slot for the
        // same index — wasting capacity and, when the gap finally
        // closes, delivering that payload to the game layer twice.
        // Same bytes = genuine duplicate; different bytes = a new message
        // reusing the index, which replaces the held one.
        int hold_replace = -1;
        for (int k = 0; k < MCP_ORDER_HOLD_CAP; ++k) {
            if (!s->order_hold[chan][k].used)
                continue;
            if (s->order_hold[chan][k].order_index != order_index)
                continue;
            if (s->order_hold[chan][k].len == plen &&
                memcmp(s->order_hold[chan][k].data, payload, plen) == 0)
                return; // already holding these exact bytes
            hold_replace = k;
            break;
        }
        if (hold_replace >= 0) {
            uint8_t* copy = (uint8_t*)malloc(plen);
            if (copy) {
                memcpy(copy, payload, plen);
                free(s->order_hold[chan][hold_replace].data);
                s->order_hold[chan][hold_replace].data = copy;
                s->order_hold[chan][hold_replace].len = plen;
                s->order_hold[chan][hold_replace].stored_ms = mcp_utils_time_ms();
            }
            return;
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
                s->order_hold[chan][k].used = true;
                s->order_hold[chan][k].order_index = order_index;
                s->order_hold[chan][k].data = copy;
                s->order_hold[chan][k].len = plen;
                s->order_hold[chan][k].stored_ms = mcp_utils_time_ms();
                stored = true;
                // Diagnostics: track how full this ever gets.
                uint32_t used_now = 0;
                for (int kk = 0; kk < MCP_ORDER_HOLD_CAP; ++kk)
                    if (s->order_hold[chan][kk].used)
                        used_now++;
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
    order_remember_delivered(s, chan, order_index, payload, plen);
    handle_encapsulated(sm, s, payload, plen);

    bool advanced = true;
    while (advanced) {
        advanced = false;
        for (int k = 0; k < MCP_ORDER_HOLD_CAP; ++k) {
            if (s->order_hold[chan][k].used &&
                s->order_hold[chan][k].order_index == s->recv_order_index[chan]) {
                uint8_t* held_data = s->order_hold[chan][k].data;
                size_t held_len = s->order_hold[chan][k].len;
                // Record the HELD packet's own index, not the index of the
                // packet that just closed the gap.
                uint32_t held_index = s->order_hold[chan][k].order_index;
                s->order_hold[chan][k].used = false;
                s->order_hold[chan][k].data = NULL;
                s->recv_order_index[chan] = (s->recv_order_index[chan] + 1) & MCP_SEQ24_MASK;
                s->recv_order_advanced_ms[chan] = mcp_utils_time_ms();
                if (held_data) {
                    order_remember_delivered(s, chan, held_index, held_data, held_len);
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
            if (s->order_hold[chan][k].used && (now - s->order_hold[chan][k].stored_ms) > 10000) {
                any_expired = true;
                break;
            }
        }
        if (!any_expired)
            continue;

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
            if (s->order_hold[chan][k].used && (now - s->order_hold[chan][k].stored_ms) > 10000) {
                if (s->order_hold[chan][k].order_index == s->recv_order_index[chan]) {
                    // This shouldn't normally happen (a slot holding
                    // exactly the currently-expected index would have
                    // been delivered already by deliver_ordered's drain
                    // loop), but handle it defensively rather than assume.
                    expected_was_freed = true;
                }
                if (s->order_hold[chan][k].data)
                    free(s->order_hold[chan][k].data);
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
                if (s->order_hold[chan][k].used) {
                    something_waiting_ahead = true;
                    break;
                }
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
static void handle_encapsulated(mcp_rak_session_manager_t* sm, mcp_rak_session_t* s,
                                const uint8_t* payload, size_t plen) {
    if (plen < 1)
        return;
    uint8_t pid = payload[0];
    uint8_t dgram[RAKLIB_MAX_MTU + 128];
    size_t dgram_len = 0;
    char ipStr[20];

    if (s->state == MCP_SESSION_STATE_CONNECTING_2) {
        // ---- CLIENT_CONNECT (0x09) ----
        if (pid == 0x09 && plen >= 17) {
            size_t i = 1;
            int64_t client_id = rb_i64_be(payload, plen, &i);
            int64_t send_ping = (int64_t)rb_u64_be(payload, plen, &i);
            s->client_id = client_id;

            // Reply: SERVER_HANDSHAKE (0x10).
            //
            // FIX (root cause of "never opens a session, then TIMEOUT" on
            // lossy paths — mobile CGNAT, WiFi, shared hosting panels): this
            // reply used to be built by hand and fired off as a raw,
            // UNRELIABLE datagram straight to the socket. Unreliable means
            // the client never ACKs it, so ONE lost datagram was gone
            // forever: the client sits waiting for the handshake, gives up
            // after ~10s of silence, and the server keeps the half-open
            // session until the 30s sweep prints exactly the
            // recv_packets=1 / sent_packets=0 / TIMEOUT dump from the
            // Magmanode report. Wired, low-loss paths (and the playit.gg
            // relay, a clean server-to-server hop) rarely lose it — which
            // is why the tunnel "fixed" it while Magmanode/local kept
            // failing. Real RakNet sends ConnectionRequestAccepted as
            // RELIABLE_ORDERED on channel 0: the client ACKs or NACKs it,
            // the NACK path resends from the recovery queue, and the 5s
            // stale-retransmit sweep covers outright loss. Going through
            // the standard encapsulated send path ALSO feeds
            // diag_sent_packets/last_packet_sent_ms, so the disconnect
            // dump stops lying about the handshake phase.
            dgram_len = build_server_handshake(dgram, sizeof(dgram), s->ip, s->port, send_ping);
            if (dgram_len == 0 ||
                !mcp_rak_session_manager_send_encapsulated(sm, s->ip, s->port, dgram, dgram_len,
                                                           MCP_RELIABILITY_RELIABLE_ORDERED, 0)) {
                fprintf(stderr,
                        "[NET] WARNING: SERVER_HANDSHAKE for %u.%u.%u.%u:%u could "
                        "not be sent (build failed or socket send failed) — the "
                        "client will time out.\n",
                        s->ip[0], s->ip[1], s->ip[2], s->ip[3], s->port);
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
        if (mcp_debug_enabled() && pid != 0x13 && pid != 0x15) {
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
            // The reference server default: portChecking enabled
            if (srv_port == sm->bind_port || true /* portChecking off for MCPE */) {
                s->state = MCP_SESSION_STATE_CONNECTED;
                // Notify upper layer
                if (sm->instance && sm->instance->on_open_session) {
                    snprintf(ipStr, sizeof(ipStr), "%u.%u.%u.%u", s->ip[0], s->ip[1], s->ip[2],
                             s->ip[3]);
                    sm->instance->on_open_session(sm->instance->impl, ipStr, s->port, s->client_id);
                }
            }
            return;
        }

        // ---- CLIENT_DISCONNECT (0x15) ----
        if (pid == 0x15) {
            if (sm->instance && sm->instance->on_close_session) {
                snprintf(ipStr, sizeof(ipStr), "%u.%u.%u.%u", s->ip[0], s->ip[1], s->ip[2],
                         s->ip[3]);
                sm->instance->on_close_session(sm->instance->impl, ipStr, s->port,
                                               "client disconnect");
            }
            s->is_disconnecting = true; // Bug 2 fix: cleanup next tick
            return;
        }
    }

    if (s->state == MCP_SESSION_STATE_CONNECTED) {
        // PING (0x00) is a RakNet-level keepalive. It does NOT mean the
        // client is actively playing — but it DOES mean the connection is
        // still alive.
        //
        // FIX (AFK kick): last_activity_ms is the CONNECTION-alive clock
        // (what the timeout sweep checks), and every datagram touches it —
        // PINGs included — exactly like RakLib's Connection::lastUpdate.
        // Previously only non-PING game packets reset it, so an idle
        // player (AFK: no input, just keepalives) was force-disconnected
        // after 30s even though their connection was perfectly healthy.
        //
        // Whether the client is actually still at the controls is tracked
        // separately by last_game_packet_received_ms (purely diagnostic),
        // so a zombie background session is still visible in the
        // disconnect log without ever kicking a live idle player.
        bool is_ping = (pid == 0x00);

        s->last_activity_ms = mcp_utils_time_ms();
        s->diag_datagrams_received++;
        s->is_active = true;
        if (!is_ping) {
            s->last_game_packet_received_ms = s->last_activity_ms;
        } else {
            s->last_ping_received_ms = s->last_activity_ms;
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
            size_t dp_len = mcp_rak_session_build_datagram(
                s, dp, sizeof(dp), dgram, dgram_len, MCP_RELIABILITY_UNRELIABLE, 0, false, 0, 0, 0);
            // FIX: count this send (see sm_send_reply) so the disconnect
            // dump reflects keepalive traffic instead of always showing
            // sent_packets frozen at 0.
            if (dp_len > 0)
                sm_send_reply(sm, s, dp, dp_len, s->ip, s->port);
            return;
        }

        // ---- CLIENT_DISCONNECT (0x15) ----
        if (pid == 0x15) {
            log_session_diagnostics(s, "CLIENT_DISCONNECT");
            if (sm->instance && sm->instance->on_close_session) {
                snprintf(ipStr, sizeof(ipStr), "%u.%u.%u.%u", s->ip[0], s->ip[1], s->ip[2],
                         s->ip[3]);
                sm->instance->on_close_session(sm->instance->impl, ipStr, s->port,
                                               "client disconnect");
            }
            s->is_disconnecting = true; // Bug 2 fix: cleanup next tick
            return;
        }

        // ---- PONG (0x03): reply to OUR keepalive ping ----
        // Only the RTT matters. Previously this fell through to the game
        // layer as if it were a game packet, so a server keepalive reply
        // was handed to the packet decoder on every single idle ping.
        if (pid == 0x03 && plen >= 9) {
            size_t pi = 1;
            const int64_t id = (int64_t)rb_u64_be(payload, plen, &pi);
            const uint64_t pong_now = mcp_utils_time_ms();
            if (s->keepalive_ping_id != 0 && id == s->keepalive_ping_id &&
                pong_now >= s->keepalive_ping_sent_ms) {
                s->rtt_ms = (uint32_t)(pong_now - s->keepalive_ping_sent_ms);
                s->diag_keepalive_pongs++;
                s->keepalive_ping_id = 0;
            }
            return;
        }

        // ---- Any other packet → forward to game layer ----
        if (sm->instance && sm->instance->on_packet_receive) {
            snprintf(ipStr, sizeof(ipStr), "%u.%u.%u.%u", s->ip[0], s->ip[1], s->ip[2], s->ip[3]);
            sm->instance->on_packet_receive(sm->instance->impl, ipStr, s->port, payload, plen);
        }
    }
}

// =========================================================================
// Handle a received DATA_PACKET (0x80..0x8F)
// =========================================================================
static void handle_data_packet(mcp_rak_session_manager_t* sm, mcp_rak_session_t* s,
                               const uint8_t* data, size_t len) {
    uint32_t seq;
    size_t payload_off;
    if (!decode_data_header(data, len, &seq, &payload_off))
        return;

    // Sequence-number / window logic (mirrors the reference server Session).
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
        s->window_start = seq;           // will be re-centered below via diff==0 path
        // FIX: window_end used to stay at its init value
        // (MCP_SESSION_WINDOW_SIZE) while window_start jumped to the
        // client's actual first sequence number, leaving a lopsided window
        // [seq, 2048) whenever the first seq was large. Center it the same
        // way every later slide does.
        s->window_end = (seq + MCP_SESSION_WINDOW_SIZE / 2) & MCP_SEQ24_MASK;
    } else {
        uint32_t last = (uint32_t)s->last_recv_seq & MCP_SEQ24_MASK;
        if (seq24_less(seq, s->window_start) || !seq24_less(seq, s->window_end)) {
            return; // out of window — drop
        }
        (void)last;
    }
    uint32_t slot = seq % MCP_SESSION_WINDOW_SIZE;
    if (s->recv_window[slot])
        return; // duplicate

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
        // FIX (stall): the clear-ahead walk below used to have no bound on
        // the distance. On a session's FIRST advance, old_start is the init
        // value 0 while new_start is seq-1024 — numerically ~16.7 million
        // slots BEHIND old_start in the circular 24-bit space — so that one
        // datagram spun the loop ~16.7M times (tens of ms of hard stall on
        // the network path, right in the middle of the handshake), and any
        // large forward jump repeated it. The loop only ever needs to clear
        // the slots the window slides OVER (at most MCP_SESSION_WINDOW_SIZE
        // of them); anything farther is covered by clearing the whole array.
        uint32_t fwd = (new_start - old_start) & MCP_SEQ24_MASK;
        if (fwd >= MCP_SESSION_WINDOW_SIZE) {
            memset(s->recv_window, 0, sizeof(s->recv_window));
        } else {
            uint32_t m = old_start;
            while (m != new_start) {
                s->recv_window[m % MCP_SESSION_WINDOW_SIZE] = false;
                m = (m + 1) & MCP_SEQ24_MASK;
            }
        }
        s->last_recv_seq = (int32_t)seq;
        s->window_start = new_start;
        s->window_end = (seq + MCP_SESSION_WINDOW_SIZE / 2) & MCP_SEQ24_MASK;
    }
    // Any datagram received refreshes the session's alive clock (RakNet
    // Connection::lastUpdate semantics). A client that only sends ACKs
    // and keepalives still proves its connection is up, so it must NOT
    // be reaped by the timeout sweep — that is the AFK-kick fix.
    s->last_activity_ms = mcp_utils_time_ms();
    s->is_active = true;
    s->diag_recv_packets++;
    s->last_packet_received_ms = mcp_utils_time_ms();
    const uint8_t* ptr = data + payload_off;
    size_t rem = len - payload_off;
    while (rem > 0) {
        const uint8_t* epayload;
        size_t eplen;
        bool done;
        bool has_split = false;
        uint32_t split_count = 0, split_index = 0;
        uint16_t split_id = 0;
        uint8_t reliability = 0;
        bool is_reliable = false;
        uint32_t message_index = 0;
        bool is_ordered = false;
        uint32_t order_index = 0;
        uint8_t order_channel = 0;

        size_t consumed = parse_encap_record(
            ptr, rem, &epayload, &eplen, &done, &has_split, &split_count, &split_id, &split_index,
            &reliability, &is_reliable, &message_index, &is_ordered, &order_index, &order_channel);
        if (consumed == 0 || done)
            break;

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
        if (is_reliable && !has_split &&
            !reliable_index_accept(s, message_index, epayload, eplen)) {
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
                    if (!s->split_slots[si].in_use) {
                        slot = &s->split_slots[si];
                        break;
                    }
                }
            }
            if (!slot) {
                // All slots occupied by distinct, still-incomplete splits —
                // evict the one that hasn't seen a fragment in the longest
                // time (almost certainly the stalest / most likely dead).
                int oldest = 0;
                for (int si = 1; si < MCP_MAX_CONCURRENT_SPLITS; ++si) {
                    if (s->split_slots[si].last_fragment_ms <
                        s->split_slots[oldest].last_fragment_ms)
                        oldest = si;
                }
                for (uint32_t k = 0; k < s->split_slots[oldest].count && k < MCP_MAX_SPLIT_SEGMENTS;
                     ++k) {
                    if (s->split_slots[oldest].segments[k]) {
                        free(s->split_slots[oldest].segments[k]);
                        s->split_slots[oldest].segments[k] = NULL;
                    }
                }
                s->diag_split_evicted++;
                slot = &s->split_slots[oldest];
                memset(slot, 0, sizeof(*slot));
            }

            // A fragment whose declared split_count disagrees with the count
            // the slot was created with would let segments land at indexes
            // past the bound every free path walks (all of them stop at
            // slot->count). Drop the fragment instead.
            if (slot->in_use && slot->count != split_count) {
                ptr += consumed;
                rem -= consumed;
                continue;
            }

            if (!slot->in_use) {
                // Freshly claimed slot — initialize for this split_id.
                memset(slot, 0, sizeof(*slot));
                slot->split_id = split_id;
                slot->count = split_count;
                slot->in_use = true;
                // Ordering info comes from the first fragment seen — every
                // fragment of one split packet shares the same
                // order_index/order_channel (the sender only allocates
                // one), so capturing it once here is correct.
                slot->is_ordered = is_ordered;
                slot->order_index = order_index;
                slot->order_channel = order_channel;
                // Same reasoning applies to reliability/message_index —
                // see the comment on these fields in Session.h.
                slot->is_reliable = is_reliable;
                slot->message_index = message_index;
                slot->started_ms = mcp_utils_time_ms();
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
                // Calculate total size
                size_t total = 0;
                for (uint32_t k = 0; k < slot->count; ++k) {
                    total += slot->seg_sizes[k];
                }

                // Concatenate all fragments FIRST, so the duplicate check
                // below can compare real bytes. It used to run BEFORE
                // reassembly with a NULL payload, and
                // reliable_already_delivered() bails out on NULL — so the
                // check was effectively disabled and a retransmitted split
                // message was handed to the game layer twice.
                uint8_t* reassembled = (total > 0) ? (uint8_t*)malloc(total) : NULL;
                if (total == 0) {
                    // Nothing to deliver (every segment empty) — just retire
                    // the slot below.
                } else if (reassembled) {
                    size_t off = 0;
                    for (uint32_t k = 0; k < slot->count; ++k) {
                        if (slot->segments[k]) {
                            memcpy(reassembled + off, slot->segments[k], slot->seg_sizes[k]);
                            off += slot->seg_sizes[k];
                        }
                    }
                    // The reliable-message duplicate/staleness check belongs
                    // here and only here for a fragmented message: see the
                    // long comment in reliable_index_accept(). Now that the
                    // whole message exists, the real bytes are available and
                    // a genuine retransmit of an already-delivered split is
                    // correctly recognized and dropped.
                    bool duplicate =
                        slot->is_reliable &&
                        !reliable_index_accept(s, slot->message_index, reassembled, total);
                    if (!duplicate) {
                        // Process the complete reassembled packet — through
                        // the same ordering gate as non-fragmented packets,
                        // using the ordering info captured from the first
                        // fragment.
                        deliver_ordered(sm, s, slot->is_ordered, slot->order_index,
                                        slot->order_channel, reassembled, total);
                    }
                    free(reassembled);
                }
                s->diag_split_completed++;

                // Clean up this slot only — every other in-flight split_id
                // for this session is untouched. The array bound guards the
                // walk in case slot->count ever disagrees with the stored
                // segment indexes.
                for (uint32_t k = 0; k < slot->count && k < MCP_MAX_SPLIT_SEGMENTS; ++k) {
                    if (slot->segments[k]) {
                        free(slot->segments[k]);
                        slot->segments[k] = NULL;
                    }
                }
                memset(slot, 0, sizeof(*slot));
            }
        } else {
            // Non-fragmented packet — process through the ordering gate.
            deliver_ordered(sm, s, is_ordered, order_index, order_channel, epayload, eplen);
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
        if (s->split_slots[i].in_use)
            split_active_now++;
    }
    uint32_t order_hold_active_now = 0;
    for (int c = 0; c < 32; ++c) {
        for (int k = 0; k < MCP_ORDER_HOLD_CAP; ++k) {
            if (s->order_hold[c][k].used)
                order_hold_active_now++;
        }
    }
    fprintf(
        stderr,
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
        "  datagrams_received=%llu since_last_ack_ms=%llu bytes_sent=%llu\n"
        "  recovery_occupied=%u recovery_peak=%u recovery_overwrites=%u (cap=%u)\n"
        "  inflight_peak=%u backpressure_drops=%u (cap=%u)\n"
        "  keepalive_pings=%u keepalive_pongs=%u rtt_ms=%u\n"
        "  split_active_now=%u split_completed=%llu split_expired=%u split_evicted=%u\n"
        "  order_hold_active_now=%u order_hold_peak=%u order_hold_full=%u order_duplicate=%u order_forced_skip=%u\n"
        "  order_index_reuse=%u duplicate_reliable_dropped=%u\n",
        s->ip[0], s->ip[1], s->ip[2], s->ip[3], s->port, reason,
        (unsigned long long)(now - s->start_time_ms),
        (unsigned long long)(now - s->last_packet_received_ms),
        (unsigned long long)(now - s->last_game_packet_received_ms),
        s->last_ping_received_ms ? (unsigned long long)(now - s->last_ping_received_ms) : 0ULL,
        (unsigned long long)(now - s->last_packet_sent_ms),
        (unsigned long long)s->diag_recv_packets, (unsigned long long)s->diag_sent_packets,
        (unsigned long long)s->diag_ack_received, (unsigned long long)s->diag_nack_received,
        (unsigned long long)s->diag_retransmits, (unsigned long long)s->diag_datagrams_received,
        (unsigned long long)(now - s->last_ack_received_ms), (unsigned long long)s->diag_bytes_sent,
        s->diag_recovery_occupied, s->diag_recovery_peak, s->diag_recovery_overwrites,
        (unsigned)MCP_RECOVERY_CAP, s->diag_inflight_peak, s->diag_backpressure_drops,
        (unsigned)MCP_MAX_INFLIGHT_RELIABLE, s->diag_keepalive_pings, s->diag_keepalive_pongs,
        s->rtt_ms, split_active_now, (unsigned long long)s->diag_split_completed,
        s->diag_split_expired, s->diag_split_evicted, order_hold_active_now,
        s->diag_order_hold_peak, s->diag_order_hold_full, s->diag_order_duplicate,
        s->diag_order_forced_skip, s->diag_order_index_reuse, s->diag_duplicate_dropped);
}

// =========================================================================
// Handle ACK (0xc0): remove from recovery queue
// =========================================================================
static void handle_ack(mcp_rak_session_t* s, const uint8_t* data, size_t len) {
    if (len < 3)
        return;
    s->diag_ack_received++;
    // An ACK is proof of life: the client is still receiving and
    // answering. The rest of this file documents last_activity_ms as
    // "touched by EVERY received datagram — game packets, keepalive PINGs
    // and ACK/NACKs alike", but ACK/NACK datagrams never touched it, so a
    // client that was busy acking chunk data and not sending game packets
    // could still be reaped by the timeout sweep. Make the code match the
    // contract.
    const uint64_t now_ack = mcp_utils_time_ms();
    s->diag_datagrams_received++;
    s->last_packet_received_ms = now_ack;
    s->last_activity_ms = now_ack;
    s->last_ack_received_ms = now_ack;
    size_t i = 1;
    uint16_t count = rb_u16_be(data, len, &i);
    for (uint16_t k = 0; k < count && i < len; ++k) {
        uint8_t record_type = data[i++];
        if (i + 3 > len)
            break;
        uint32_t lo = rb_u24_le(data, len, &i);
        uint32_t hi = lo;
        if (record_type == 0x00) { // range
            if (i + 3 > len)
                break;
            hi = rb_u24_le(data, len, &i);
        }
        // SECURITY FIX: a corrupt or malicious ACK can claim an enormous
        // range (e.g. lo=0, hi=0xFFFFFF). Without a cap, the loop below
        // can iterate billions of times, hanging the RakLib thread for
        // seconds — which then starves every session on the server,
        // looking exactly like a timeout/disconnect to players. Real
        // ACK ranges only ever cover recently-sent sequence numbers,
        // which can never span more than the recovery window.
        if (hi < lo || (hi - lo) >= MCP_RECOVERY_CAP)
            continue;
        for (uint32_t seq = lo; seq <= hi; ++seq) {
            uint32_t slot = seq % MCP_RECOVERY_CAP;
            if (s->recovery[slot].data && s->recovery[slot].seq == seq) {
                free(s->recovery[slot].data);
                s->recovery[slot].data = NULL;
                s->recovery[slot].len = 0;
                if (s->diag_recovery_occupied > 0)
                    s->diag_recovery_occupied--;
            }
        }
    }
    // ACKs refresh the alive clock too: they prove the client is still
    // on the other end (AFK-kick fix — same rule as every other packet).
    s->last_activity_ms = mcp_utils_time_ms();
}
// =========================================================================
// Handle NACK (0xa0): retransmit from recovery
// =========================================================================
static void handle_nack(mcp_rak_session_manager_t* sm, mcp_rak_session_t* s, const uint8_t* data,
                        size_t len) {
    if (len < 3)
        return;
    s->diag_nack_received++;
    s->diag_datagrams_received++;
    size_t i = 1;
    uint16_t count = rb_u16_be(data, len, &i);
    for (uint16_t k = 0; k < count && i < len; ++k) {
        uint8_t record_type = data[i++];
        if (i + 3 > len)
            break;
        uint32_t lo = rb_u24_le(data, len, &i);
        uint32_t hi = lo;
        if (record_type == 0x00) {
            if (i + 3 > len)
                break;
            hi = rb_u24_le(data, len, &i);
        }
        // SECURITY FIX: same unbounded-range hang as handle_ack() above.
        if (hi < lo || (hi - lo) >= MCP_RECOVERY_CAP)
            continue;
        for (uint32_t seq = lo; seq <= hi; ++seq) {
            uint32_t slot = seq % MCP_RECOVERY_CAP;
            if (s->recovery[slot].data && s->recovery[slot].seq == seq) {
                mcp_rak_session_manager_send(sm, s->recovery[slot].data, s->recovery[slot].len,
                                             s->ip, s->port);
            }
        }
    }
    // NACKs refresh the alive clock too — same rule as ACKs (AFK-kick fix).
    s->last_packet_received_ms = mcp_utils_time_ms();
    s->last_activity_ms = mcp_utils_time_ms();
}

// =========================================================================
// Handle unconnected (offline) packets
// =========================================================================
static void handle_unconnected(mcp_rak_session_manager_t* sm, const uint8_t* data, size_t len,
                               const uint8_t ip[4], uint16_t port) {
    if (len < 1)
        return;
    uint8_t pid = data[0];
    uint8_t out[512];
    size_t out_len = 0;

    // ---- UNCONNECTED_PING (0x01) / PING_OPEN_CONNECTIONS (0x02) ----
    if (pid == 0x01 || pid == 0x02) {
        if (len < 17) {
            note_handshake_drop(data, len, ip, port, "unconnected ping shorter than 17 bytes");
            return;
        }
        size_t pi = 1;
        uint64_t ping_time = rb_u64_le(data, len, &pi);
        out_len =
            build_unconnected_pong(out, sizeof(out), ping_time, sm->server_id, sm->server_name);
        // FIX: count this reply (see sm_send_reply) so sends are visible
        // in the disconnect dump instead of always reporting 0.
        sm_send_reply(sm, NULL, out, out_len, ip, port);
        return;
    }

    // ---- OPEN_CONNECTION_REQUEST_1 (0x05) ----
    if (pid == 0x05) {
        uint8_t protocol;
        uint16_t mtu;
        if (!decode_ocr1(data, len, &protocol, &mtu)) {
            note_handshake_drop(data, len, ip, port,
                                "OPEN_CONNECTION_REQUEST_1 needs at least 18 bytes");
            return;
        }

        // Protocol check: MCPE 0.15.10 uses RakNet protocol 6
        // We accept 6 and also 8 (some clients send 8).
        // If wrong protocol we could reject, but the reference server just ignores; we accept.

        mcp_rak_session_t* s = mcp_rak_session_manager_find(sm, ip, port);
        if (s && s->state > MCP_SESSION_STATE_CONNECTING_1) {
            // FIX (state downgrade): a duplicate/late OCR1 used to RESET a
            // session that had already progressed (CONNECTING_2/CONNECTED)
            // back to CONNECTING_1 via get_or_create + unconditional state
            // assignment. On a lossy path the client losing REPLY_2 and
            // restarting its handshake would silently tear down progress it
            // had already made; its very next CLIENT_CONNECT /
            // CLIENT_HANDSHAKE then arrived at a session in the wrong state
            // and was silently ignored — the "never opens a session"
            // signature. A still-alive session now just gets a fresh REPLY_1
            // so the client can continue WITHOUT losing state; only a dead
            // session (silent >10s) is reset for a new handshake attempt.
            uint64_t now = mcp_utils_time_ms();
            uint64_t last_seen = s->last_activity_ms > s->last_packet_received_ms
                                     ? s->last_activity_ms
                                     : s->last_packet_received_ms;
            if ((now - last_seen) <= 10000) {
                out_len = build_ocr1_reply(out, sizeof(out), sm->server_id, mtu);
                sm_send_reply(sm, s, out, out_len, ip, port);
                return;
            }
        }
        if (!s) {
            s = mcp_rak_session_manager_get_or_create(sm, ip, port);
            if (!s) {
                note_handshake_drop(data, len, ip, port,
                                    "no free session slot for OPEN_CONNECTION_REQUEST_1");
                return;
            }
        }
        // Cap by the SERVER's configured MTU as well as the global max.
        // The client probes with whatever it is willing to accept (usually
        // the full 1492), but the server may be sitting behind a path with a
        // smaller MTU — a VPS bridge, a VXLAN/overlay tunnel, WSL2 (1350).
        // Those send with the don't-fragment bit set, so a 1492-byte IP
        // packet is DROPPED rather than fragmented and no split ever
        // completes. Taking min(client, sm->mtu) is what makes the server
        // work on both a plain LAN and a hosted Linux box.
        if (sm->mtu && mtu > sm->mtu)
            mtu = sm->mtu;

        s->mtu = mtu;
        s->state = MCP_SESSION_STATE_CONNECTING_1;
        s->last_activity_ms = mcp_utils_time_ms();

        out_len = build_ocr1_reply(out, sizeof(out), sm->server_id, mtu);
        sm_send_reply(sm, s, out, out_len, ip, port);
        return;
    }

    // ---- OPEN_CONNECTION_REQUEST_2 (0x07) ----
    if (pid == 0x07) {
        mcp_rak_session_t* s = mcp_rak_session_manager_find(sm, ip, port);
        uint16_t mtu;
        int64_t client_id;
        uint16_t srv_port;
        // Decode BEFORE creating anything: a malformed request must not
        // leave a ~hundreds-of-KB session behind (they are only reaped by
        // the 30 s timeout sweep).
        if (!decode_ocr2(data, len, &mtu, &client_id, &srv_port)) {
            note_handshake_drop(data, len, ip, port,
                                "OPEN_CONNECTION_REQUEST_2 too short to parse");
            return;
        }
        if (!s) {
            // Can create fresh if state was lost
            s = mcp_rak_session_manager_get_or_create(sm, ip, port);
            if (!s) {
                note_handshake_drop(data, len, ip, port,
                                    "no free session slot for OPEN_CONNECTION_REQUEST_2");
                return;
            }
        }

        // Clamp MTU like the reference server, then cap it by the server's own configured
        // MTU (see the OCR1 handler for why): the client asking for 1492
        // does not make 1492 safe on every path.
        if (mtu > RAKLIB_MAX_MTU)
            mtu = RAKLIB_MAX_MTU;
        if (sm->mtu && mtu > sm->mtu)
            mtu = sm->mtu;
        if (mtu < RAKLIB_MIN_MTU)
            mtu = RAKLIB_MIN_MTU;

        // FIX (state downgrade): same guard as the OCR1 handler — a
        // duplicate/retransmitted OCR2 must not reset a session that is
        // already in CONNECTING_2 or CONNECTED (it used to assign
        // state=CONNECTING_2 unconditionally, clobbering a live session).
        // Reply so the client can proceed, keep the state.
        if (s->state != MCP_SESSION_STATE_UNCONNECTED &&
            s->state != MCP_SESSION_STATE_CONNECTING_1) {
            uint64_t now = mcp_utils_time_ms();
            uint64_t last_seen = s->last_activity_ms > s->last_packet_received_ms
                                     ? s->last_activity_ms
                                     : s->last_packet_received_ms;
            if ((now - last_seen) <= 10000) {
                out_len = build_ocr2_reply(out, sizeof(out), sm->server_id, ip, port, mtu);
                sm_send_reply(sm, s, out, out_len, ip, port);
                return;
            }
        }

        s->mtu = mtu;
        s->client_id = client_id;
        s->state = MCP_SESSION_STATE_CONNECTING_2;
        s->last_activity_ms = mcp_utils_time_ms();

        out_len = build_ocr2_reply(out, sizeof(out), sm->server_id, ip, port, mtu);
        sm_send_reply(sm, s, out, out_len, ip, port);
        return;
    }

    // ---- Everything else → raw packet callback ----
    if (sm->instance && sm->instance->on_raw_packet) {
        char ipStr[20];
        snprintf(ipStr, sizeof(ipStr), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
        sm->instance->on_raw_packet(sm->instance->impl, ipStr, port, data, len);
    }
}

// =========================================================================
// Flush per-session ACK + NACK
// =========================================================================
static void flush_ack_nack(mcp_rak_session_manager_t* sm, mcp_rak_session_t* s) {
    uint32_t mins[64], maxs[64];

    size_t na = mcp_rak_session_drain_ack_queue(s, mins, maxs, 64);
    if (na > 0) {
        uint8_t ackBuf[512];
        size_t alen = build_ack_pkt(ackBuf, sizeof(ackBuf), mins, maxs, na, false);
        if (alen > 0)
            mcp_rak_session_manager_send(sm, ackBuf, alen, s->ip, s->port);
    }

    size_t nn = mcp_rak_session_drain_nack_queue(s, mins, maxs, 64);
    if (nn > 0) {
        uint8_t nackBuf[512];
        size_t nlen = build_ack_pkt(nackBuf, sizeof(nackBuf), mins, maxs, nn, true);
        if (nlen > 0)
            mcp_rak_session_manager_send(sm, nackBuf, nlen, s->ip, s->port);
    }
}

// =========================================================================
// Retransmit recovery entries that have been silent for too long
//
// Three changes over the previous fixed 5s / 32-per-session-per-tick
// behaviour, all aimed at the same failure: ONE stalled client soaking
// the single network thread that every player shares.
//
//  * a session with nothing in flight skips the 4096-slot scan entirely
//    (with 20 idle players this used to be 82k useless iterations/tick);
//  * each entry backs off exponentially (5s, 10s, 20s, 40s) instead of
//    being retried at the same rate forever;
//  * the budget is GLOBAL for the whole tick (MCP_RETRANSMIT_MAX_PER_TICK)
//    and per session on top of that (MCP_RETRANSMIT_MAX_PER_SESSION), so a
//    session that never acknowledges can never starve the others.
// =========================================================================
static void retransmit_stale(mcp_rak_session_manager_t* sm, mcp_rak_session_t* s,
                             size_t* global_budget) {
    if (s->diag_recovery_occupied == 0)
        return;
    if (!global_budget || *global_budget == 0)
        return;

    uint64_t now = mcp_utils_time_ms();
    size_t sent_this_session = 0;
    for (size_t k = 0; k < MCP_RECOVERY_CAP; ++k) {
        if (*global_budget == 0)
            break;
        if (sent_this_session >= MCP_RETRANSMIT_MAX_PER_SESSION)
            break;
        mcp_send_slot_t* slot = &s->recovery[k];
        if (!slot->data)
            continue;

        uint64_t interval = MCP_RETRANSMIT_BASE_MS;
        for (uint8_t i = 0; i < slot->retries && interval < MCP_RETRANSMIT_MAX_MS; ++i)
            interval *= 2;
        if (interval > MCP_RETRANSMIT_MAX_MS)
            interval = MCP_RETRANSMIT_MAX_MS;
        if ((now - slot->send_time_ms) <= interval)
            continue;

        mcp_rak_session_manager_send(sm, slot->data, slot->len, s->ip, s->port);
        slot->send_time_ms = now;
        if (slot->retries < 255)
            slot->retries++;
        s->diag_retransmits++;
        ++sent_this_session;
        --(*global_budget);
    }
}

// =========================================================================
// Main tick
// =========================================================================
void mcp_rak_session_manager_tick(mcp_rak_session_manager_t* sm) {
    if (!sm || sm->socket.fd < 0)
        return;

    uint8_t buf[RAKLIB_MAX_MTU + 8];
    uint8_t ip[4];
    uint16_t port;

    // Reset per-IP flood counter
    sm->ip_sec_n = 0;

    // Receive up to 128 datagrams per tick
    for (int n = 0; n < 128; ++n) {
        ssize_t r = mcp_udp_socket_recv(&sm->socket, buf, sizeof(buf), ip, &port);
        if (r <= 0)
            break;
        if (r < 1)
            continue;

        uint8_t pid = buf[0];

        if (pid >= 0x80 && pid <= 0x8F) {
            // Bug 3 fix: use find() instead of get_or_create() for
            // DATA_PACKETs. Connected DATA_PACKETs should only arrive
            // for sessions that already completed the handshake. Using
            // get_or_create would recreate zombie sessions for in-flight
            // retransmits arriving after a client disconnect.
            mcp_rak_session_t* s = mcp_rak_session_manager_find(sm, ip, port);
            if (s)
                handle_data_packet(sm, s, buf, (size_t)r);
            // else: stale packet from a dead session — ignore silently
        } else if (pid == 0xc0 || pid == 0xc1 || pid == 0xc2 || pid == 0xc3) {
            // ACK
            mcp_rak_session_t* s = mcp_rak_session_manager_find(sm, ip, port);
            if (s) {
                handle_ack(s, buf, (size_t)r);
                // ACKs freed recovery slots — replay anything the
                // inflight ceiling deferred.
                session_backlog_flush(sm, s);
            }
        } else if (pid == 0xa0 || pid == 0xa1 || pid == 0xa2 || pid == 0xa3) {
            // NACK
            mcp_rak_session_t* s = mcp_rak_session_manager_find(sm, ip, port);
            if (s)
                handle_nack(sm, s, buf, (size_t)r);
        } else {
            // Offline / unconnected packets
            handle_unconnected(sm, buf, (size_t)r, ip, port);
        }
    }

    // Per-session post-tick: ACK/NACK flush + retransmit + timeout sweep
    uint64_t now = mcp_utils_time_ms();
    // ONE retransmit budget for the whole tick, shared by every session.
    size_t retransmit_budget = MCP_RETRANSMIT_MAX_PER_TICK;
    for (size_t i = 0; i < sm->session_count;) {
        mcp_rak_session_t* s = sm->sessions[i];

        flush_ack_nack(sm, s);
        retransmit_stale(sm, s, &retransmit_budget);
        // Safety-net replay: covers capacity freed outside the ACK branch
        // (e.g. retransmit-path bookkeeping) and sessions whose ACKs are
        // coalesced into datagrams processed before this sweep.
        session_backlog_flush(sm, s);

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
            if (s->split_slots[si].in_use && (now - s->split_slots[si].started_ms) > 10000) {
                for (uint32_t k = 0; k < s->split_slots[si].count && k < MCP_MAX_SPLIT_SEGMENTS;
                     ++k) {
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

        // Timeout (30s, RakLib/the reference server default — see MCP_SESSION_TIMEOUT_MS).
        // last_activity_ms is refreshed by EVERY received datagram — game
        // packets, keepalive PINGs and ACK/NACKs alike — so the sweep only
        // removes sessions whose connection is genuinely dead (no datagram
        // at all for the whole window). An AFK player keeps sending
        // keepalives, so they are never kicked for being idle.
        //
        // (Historical note: this used to fire on "no non-PING game packet
        // for 15s/30s", which force-disconnected healthy idle players —
        // the AFK kick that players kept reporting.)
        bool should_remove =
            s->is_disconnecting || mcp_rak_session_is_timed_out(s, now, MCP_SESSION_TIMEOUT_MS);

        // ---- Server keepalive + liveness instrumentation ---------------
        // A session that has had nothing to say for MCP_KEEPALIVE_PING_MS
        // gets a PING and, when the PONG comes back, a real RTT. That is
        // the only thing that separates "the client died" from "the client
        // is alive but its uplink is drowning in our chunk data" — the
        // ambiguity behind every "the second player joins and then the
        // game closes for everybody" report. The health line prints while
        // it is happening, not 30 seconds later in a disconnect dump.
        if (!should_remove && s->state == MCP_SESSION_STATE_CONNECTED &&
            (now - s->last_packet_sent_ms) >= MCP_KEEPALIVE_PING_MS &&
            s->diag_recovery_occupied == 0) {
            const int64_t ping_id =
                (int64_t)now ^ ((int64_t)s->port << 32) ^ (int64_t)MCP_KEEPALIVE_PING_ID_BASE;
            uint8_t dgram[32];
            size_t oi = 0;
            dgram[oi++] = 0x00; // PING
            write_i64_be(dgram, &oi, ping_id);
            uint8_t dp[64];
            size_t dp_len = mcp_rak_session_build_datagram(
                s, dp, sizeof(dp), dgram, oi, MCP_RELIABILITY_UNRELIABLE, 0, false, 0, 0, 0);
            if (dp_len > 0) {
                s->diag_keepalive_pings++;
                s->keepalive_ping_sent_ms = now;
                s->keepalive_ping_id = ping_id;
                sm_send_reply(sm, s, dp, dp_len, s->ip, s->port);
            }
        }
        if (!should_remove && (now - s->last_health_log_ms) >= MCP_KEEPALIVE_PING_MS) {
            s->last_health_log_ms = now;
            // Only flag what is actually wrong: data we sent is still not
            // acknowledged, or the client has gone completely silent.
            // "No ACK for 5s" on its own is normal for an idle player
            // (it has nothing reliable in flight to acknowledge), so it
            // is printed, not treated as a fault.
            if ((s->diag_recovery_occupied > 0 ||
                 (now - s->last_activity_ms) > MCP_KEEPALIVE_PING_MS) &&
                mcp_debug_enabled()) {
                fprintf(stderr,
                        "[NET] health %u.%u.%u.%u:%u inflight=%u/%u "
                        "backpressure_drops=%u since_last_datagram=%llums "
                        "since_last_ack=%llums rtt=%ums retransmits=%llu "
                        "keepalive=%u/%u\n",
                        s->ip[0], s->ip[1], s->ip[2], s->ip[3], s->port, s->diag_recovery_occupied,
                        (unsigned)MCP_MAX_INFLIGHT_RELIABLE, s->diag_backpressure_drops,
                        (unsigned long long)(now - s->last_packet_received_ms),
                        (unsigned long long)(now - s->last_ack_received_ms), s->rtt_ms,
                        (unsigned long long)s->diag_retransmits, s->diag_keepalive_pongs,
                        s->diag_keepalive_pings);
            }
        }
        if (should_remove) {
            // Only call on_close_session for timeout — NOT for disconnect
            // (the disconnect handler already called it when 0x15 arrived).
            if (!s->is_disconnecting && sm->instance && sm->instance->on_close_session) {
                log_session_diagnostics(s, "TIMEOUT");
                char ipStr[20];
                snprintf(ipStr, sizeof(ipStr), "%u.%u.%u.%u", s->ip[0], s->ip[1], s->ip[2],
                         s->ip[3]);
                sm->instance->on_close_session(sm->instance->impl, ipStr, s->port, "timeout");
            }
            mcp_rak_session_destroy(s);
            free(s);
            sm->sessions[i] = sm->sessions[--sm->session_count];
        } else {
            ++i;
        }
    }
}
