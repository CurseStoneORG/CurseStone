/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

 * CurseStone | Open Source Server Software
 *
 * Copyright (C) 2026 CurseStoneORG
 *
 * Developed by CurseStoneORG.
 * Built for an open and extensible Minecraft server ecosystem.
 *
 * Licensed under the GNU Lesser General Public License v3.0 or later.
 * You may study, modify, and redistribute this software under its license.
 *
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 * Repository: https://github.com/CurseStoneORG/CurseStone
 *
 */
// SPDX-License-Identifier: LGPL-3.0-or-later
// SessionManager.h — RakNet session manager, Protocol 84 / MCPE 0.15.10
// Ported from raklib/server/SessionManager
#pragma once

#include "Session.h"
#include "ServerInstance.h"
#include "UDPServerSocket.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>


#define MCP_SM_PACKET_LIMIT 250 // default datagram budget per endpoint per recv sweep
#define MCP_SM_PORT_CHECKING true
// Hard cap on live sessions. Each mcp_rak_session_t is a few hundred KB of
// fixed arrays (recv/reliable windows, recovery ring, split slots, order
// holds); without a cap, unauthenticated 0x05/0x07 traffic from many source
// ports allocates memory faster than the 30 s timeout sweep can reap it.
// 512 covers max-players plus handshake overlap comfortably.
#define MCP_SM_MAX_SESSIONS 512

// Endpoint index size (must stay a power of two; >= 2x MAX_SESSIONS).
#define MCP_SM_MAP_BITS 10
#define MCP_SM_MAP_SIZE (1u << MCP_SM_MAP_BITS)

// =========================================================================
// Traffic pacing tables
// =========================================================================
// Every datagram that enters the manager crosses two fixed-size, O(1)
// open-addressing tables BEFORE any parsing happens:
//
//   * endpoint table (key = ip:port) — a per-sweep datagram budget. A
//     source that exceeds it has its excess datagrams dropped right at the
//     recv loop; three consecutive over-budget sweeps start pacing the
//     endpoint. This is what keeps one noisy source from consuming the
//     whole per-sweep drain cap.
//
//   * source-IP table (key = ip) — per-SECOND budgets for the unconnected
//     packet classes (0x01/0x02 pings, 0x05/0x07 handshake requests, fresh
//     session creations) and for concurrent sessions per IP. These are the
//     vectors that cost server work without ever authenticating: pings
//     cost a reply, handshake storms cost session allocations, and both
//     arrive from ports that rotate faster than any per-endpoint state can
//     track. Over budget -> the packet is dropped and the source strikes
//     once; when strikes reach the limit the source is PACED: every
//     unconnected datagram from it is silently ignored for a hold that
//     doubles per repeat offense up to the ceiling. Established sessions
//     NEVER cross these per-IP budgets (their datagrams are dispatched by
//     endpoint before the budget check), so one abusive source on a shared
//     NAT can never starve the players behind the same IP, and an honest
//     in-game client cannot pace itself out by talking to its own server.
//
// Both tables live in fixed arrays inside the manager (memset()-able, zero
// allocation on the hot path). Entries expire after expire_ms idle and the
// whole set is swept once per second by the manager tick: idle entries are
// evicted (backward-shift, like the session index) and strikes decay after
// ten quiet seconds. When pacing is disabled the recv loop skips the whole
// gate: disabled costs literally nothing.
//
// The budgets are operator-tunable from cursestone.yml (traffic.* keys are
// read by Server::loadConfig and applied through
// mcp_rak_session_manager_apply_limits before the socket binds).
typedef struct mcp_traffic_limits {
    bool enabled;                    // master switch (traffic.pacing)
    uint32_t max_packets_per_tick;   // datagrams per endpoint per sweep (0 = off)
    uint32_t max_pings_per_sec;      // 0x01/0x02 per source IP per second
    uint32_t max_handshakes_per_sec; // 0x05/0x07 per source IP per second
    uint32_t max_new_sessions_per_sec; // fresh sessions per source IP per second
    uint32_t max_sessions_per_ip;    // concurrent sessions from one source IP
    uint32_t strikes_before_pacing;  // budget overflows before pacing starts
    uint32_t hold_ms;                // first pacing hold
    uint32_t hold_max_ms;            // escalation ceiling (doubles per offense)
    uint32_t expire_ms;              // idle table-entry lifetime
} mcp_traffic_limits_t;

typedef struct mcp_traffic_ep {
    uint32_t pkt_cnt;            // datagrams seen in the current sweep
    uint32_t tick_id;            // sweep id pkt_cnt belongs to (lazy reset)
    uint32_t over_ticks;         // consecutive over-budget sweeps
    uint8_t strikes;             // budget overflows since the last quiet decade
    uint8_t pad[3];
    uint64_t paced_until_ms;     // while now < this, every datagram is dropped
    uint64_t last_violation_ms;  // last strike (drives strike decay)
    uint64_t last_seen_ms;       // drives idle eviction
} mcp_traffic_ep_t;

typedef struct mcp_traffic_ip {
    uint32_t ping_cnt;          // pings in the current 1 s window
    uint32_t hs_cnt;            // handshake requests in the current window
    uint32_t ns_cnt;            // fresh sessions in the current window
    uint8_t strikes;            // budget overflows since the last quiet decade
    uint8_t pad[3];
    uint64_t ping_win_ms;       // window start (lazy 1 s reset per counter)
    uint64_t hs_win_ms;
    uint64_t ns_win_ms;
    uint64_t paced_until_ms;
    uint64_t last_violation_ms;
    uint64_t last_seen_ms;      // drives idle eviction
} mcp_traffic_ip_t;

// Table sizes (power of two). The endpoint table must absorb every distinct
// source a scan can throw at us between sweeps; 4096 covers the 512-per-sweep
// drain cap with room for the honest population. The IP table is smaller
// because a second-window budget saturates long before thousands of
// distinct IPs matter.
#define MCP_TRAFFIC_EP_BITS 12
#define MCP_TRAFFIC_EP_SIZE (1u << MCP_TRAFFIC_EP_BITS)
#define MCP_TRAFFIC_IP_BITS 10
#define MCP_TRAFFIC_IP_SIZE (1u << MCP_TRAFFIC_IP_BITS)

// Full-table sweep cadence in ticks (eviction + strike decay). The RakLib
// thread ticks at ~100 Hz, so 100 = once per second.
#define MCP_TRAFFIC_SWEEP_TICKS 100

typedef struct mcp_rak_session_manager {
    mcp_udp_socket_t socket;
    mcp_server_instance_t* instance; // not owned
    mcp_rak_session_t** sessions;
    size_t session_count;
    size_t session_capacity;
    int64_t server_id;
    uint16_t mtu;
    uint16_t bind_port; // remembered for CLIENT_HANDSHAKE check

    // MOTD / server name (UNCONNECTED_PONG)
    char server_name[256];

    // Traffic pacing (see the block comment above mcp_traffic_limits_t).
    // The limits are copied in once at startup, before the socket binds, so
    // the recv loop reads them lock-free. Both tables replace the old
    // per-tick "last 64 IPs" scratch arrays, which were only ever reset and
    // never actually consulted.
    mcp_traffic_limits_t traffic;
    uint32_t tick_id;   // incremented each tick; drives lazy per-entry resets
    uint32_t last_drain_count; // datagrams the last recv sweep processed
                               // (0 = socket ran dry: the RakLib thread may
                               // sleep its full idle cadence)

    // Debug-mode pacing diagnostics (printed once per second by the tick).
    uint64_t diag_gate_paced;   // datagrams dropped: endpoint paced
    uint64_t diag_gate_budget;  // datagrams dropped: over the sweep budget
    uint64_t diag_untracked;    // datagrams served with no table slot (fail open)
    uint64_t diag_data_pkts;    // datagrams dispatched as DATA/ACK/NACK
    uint64_t diag_unconn_pkts;  // datagrams dispatched to handle_unconnected
    uint64_t diag_seq_drop;     // DATA datagrams dropped: out of the seq window
    uint64_t diag_dup_drop;     // DATA datagrams dropped: duplicate slot
    uint64_t diag_game_fwd;     // encapsulated payloads forwarded to the game layer
    uint32_t diag_tick_us;      // last loop iteration: time spent in the tick
    uint32_t diag_pause_us;     // last loop iteration: time spent paused

    // Per-endpoint table: key = sm_ep_key(ip, port) (stored +1, 0 = empty).
    uint64_t ep_key[MCP_TRAFFIC_EP_SIZE];
    mcp_traffic_ep_t ep_val[MCP_TRAFFIC_EP_SIZE];
    size_t ep_count;
    uint32_t ep_sweep_tick; // last emergency-sweep tick (throttles to 1/tick)

    // Per-source-IP table: key = ip in host byte order (0 = empty; 0.0.0.0
    // is never a datagram source).
    uint32_t ip_key[MCP_TRAFFIC_IP_SIZE];
    mcp_traffic_ip_t ip_val[MCP_TRAFFIC_IP_SIZE];
    size_t ip_count;
    uint32_t ip_sweep_tick;

    // O(1) endpoint -> session index (open addressing, linear probe).
    // Plain arrays keep the struct memset()-able. Slots hold
    // (endpoint_key, session*); key 0 with ptr NULL means empty.
    // 1024 slots for at most MCP_SM_MAX_SESSIONS (512) sessions keeps the
    // load factor at 0.5, so probes stay short; the previous find() was a
    // linear scan over every session on every datagram.
    uint64_t map_key[MCP_SM_MAP_SIZE];
    mcp_rak_session_t* map_ptr[MCP_SM_MAP_SIZE];
} mcp_rak_session_manager_t;

void mcp_rak_session_manager_init(mcp_rak_session_manager_t* sm, int64_t server_id, uint16_t mtu,
                                  mcp_server_instance_t* instance);
void mcp_rak_session_manager_destroy(mcp_rak_session_manager_t* sm);

int mcp_rak_session_manager_bind(mcp_rak_session_manager_t* sm, const char* host, uint16_t port);

void mcp_rak_session_manager_tick(mcp_rak_session_manager_t* sm);

// Set the MOTD string (embedded in UNCONNECTED_PONG).
void mcp_rak_session_manager_set_name(mcp_rak_session_manager_t* sm, const char* name);

// Install the traffic pacing limits (cursestone.yml traffic.* keys, mapped
// by Server::loadConfig). Call BEFORE bind/start: the recv loop reads the
// copy without any synchronization. Passing NULL keeps the built-in defaults
// with pacing DISABLED.
void mcp_rak_session_manager_apply_limits(mcp_rak_session_manager_t* sm,
                                          const mcp_traffic_limits_t* limits);

mcp_rak_session_t* mcp_rak_session_manager_get_or_create(mcp_rak_session_manager_t* sm,
                                                         const uint8_t ip[4], uint16_t port);

mcp_rak_session_t* mcp_rak_session_manager_find(mcp_rak_session_manager_t* sm, const uint8_t ip[4],
                                                uint16_t port);

// Tear down the session for (ip, port), if any, keeping the owner array and
// the O(1) endpoint index coherent: the index entry is erased with the exact
// (key, pointer) pair, the session is destroyed and freed, and the owner
// array is compacted with a swap-remove. The session is located by scanning
// the owner array — never the endpoint index — so a stale index entry can
// never cause the wrong (or a reused) session to be destroyed.
// Returns true if a session was found and torn down; false if there was no
// session for that endpoint (safe no-op).
bool mcp_rak_session_manager_close(mcp_rak_session_manager_t* sm, const uint8_t ip[4],
                                   uint16_t port);

ssize_t mcp_rak_session_manager_send(mcp_rak_session_manager_t* sm, const uint8_t* data, size_t len,
                                     const uint8_t ip[4], uint16_t port);

// Send an encapsulated MCPE packet through an established session.
// order_channel is only meaningful when reliability is one of the
// *_ORDERED variants; RakNet supports up to 32 independent ordering
// channels (0-31) so ordered traffic on one channel never blocks
// packets sent on a different channel.
bool mcp_rak_session_manager_send_encapsulated(mcp_rak_session_manager_t* sm, const uint8_t ip[4],
                                               uint16_t port, const uint8_t* payload, size_t plen,
                                               uint8_t reliability, uint8_t order_channel);

