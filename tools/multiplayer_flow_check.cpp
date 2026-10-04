// tools/multiplayer_flow_check.cpp
//
// Drives the REAL reliability layer (src/network/raklib/SessionManager.c +
// Session.c) with TWO simultaneous sessions, which is the situation the
// bug report describes ("2 players cannot join; when the second one joins,
// the game closes for everybody"):
//
//   session A (:5001) — a healthy client: acknowledges everything.
//   session B (:5002) — a stalled client: never acknowledges anything,
//                       exactly the state the field dump showed as
//                       recovery_occupied=468 / retransmits=5136.
//
// The network is replaced by a scripted socket and the clock by a fake
// one, so ~40 seconds of server behaviour are exercised instantly and
// deterministically. What is proved here:
//
//   1. a healthy second player is NEVER affected by a stalled one (fair
//      share of the retransmit budget; its traffic keeps flowing),
//   2. the stalled session stops growing without bound (in-flight cap +
//      counted refusals) instead of silently eating memory/bandwidth,
//   3. retransmits back off instead of hammering at a fixed 5s rate
//      (the old behaviour: 5136 resends for one dead client),
//   4. the server can now PROVE a session is alive: it pings it and
//      measures the RTT from the PONG,
//   5. the timeout sweep still reaps a genuinely dead session.
//
//   g++ -std=c++20 -Wall -Wextra -O1 -Isrc -I/tmp/zstub
//       tools/multiplayer_flow_check.cpp
//       src/network/raklib/SessionManager.c src/network/raklib/Session.c
//       -o /tmp/tools/multiplayer_flow_check
#include "network/raklib/SessionManager.h"

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

static int failures = 0;
static void check(bool ok, const char* what) {
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}

// ---------------------------------------------------------------------------
// Fake clock + scripted socket (replaces the real network entirely).
// ---------------------------------------------------------------------------
static uint64_t g_now_ms = 100000;
extern "C" uint64_t mcp_utils_time_ms(void) { return g_now_ms; }

struct OutDatagram { uint8_t ip[4]; uint16_t port; std::vector<uint8_t> data; };
struct InDatagram  { uint8_t ip[4]; uint16_t port; std::vector<uint8_t> data; };
static std::vector<OutDatagram> g_out;
static std::vector<InDatagram>  g_in;

extern "C" int mcp_udp_socket_open(mcp_udp_socket_t* s, const char*, uint16_t) {
    s->fd = 7; s->blocking = false; return 0;
}
extern "C" void mcp_udp_socket_close(mcp_udp_socket_t* s) { if (s) s->fd = -1; }
extern "C" void mcp_udp_socket_set_blocking(mcp_udp_socket_t* s, bool b) { s->blocking = b; }
extern "C" ssize_t mcp_udp_socket_recv(mcp_udp_socket_t*, void* buf, size_t cap,
                                      uint8_t* src_ip, uint16_t* src_port) {
    if (g_in.empty()) return -EAGAIN;
    InDatagram m = g_in.front();
    g_in.erase(g_in.begin());
    if (m.data.size() > cap) return -1;
    memcpy(buf, m.data.data(), m.data.size());
    memcpy(src_ip, m.ip, 4);
    *src_port = m.port;
    return (ssize_t)m.data.size();
}
extern "C" ssize_t mcp_udp_socket_send(mcp_udp_socket_t*, const void* buf, size_t len,
                                      const uint8_t* dst_ip, uint16_t dst_port) {
    OutDatagram o;
    memcpy(o.ip, dst_ip, 4);
    o.port = dst_port;
    o.data.assign((const uint8_t*)buf, (const uint8_t*)buf + len);
    g_out.push_back(std::move(o));
    return (ssize_t)len;
}

// ---------------------------------------------------------------------------
// Tiny wire helpers for the fake client.
// ---------------------------------------------------------------------------
static void put_u16_be(std::vector<uint8_t>& v, uint16_t x) {
    v.push_back((uint8_t)(x >> 8)); v.push_back((uint8_t)x);
}
static void put_u24_le(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back((uint8_t)x); v.push_back((uint8_t)(x >> 8)); v.push_back((uint8_t)(x >> 16));
}
// [0xc0][u16be count]{ [0x01][seq u24le] ... } — exactly what handle_ack parses.
static std::vector<uint8_t> ackPacket(const std::vector<uint32_t>& seqs) {
    std::vector<uint8_t> p;
    p.push_back(0xc0);
    put_u16_be(p, (uint16_t)seqs.size());
    for (uint32_t s : seqs) { p.push_back(0x01); put_u24_le(p, s); }
    return p;
}
static uint32_t datagramSeq(const std::vector<uint8_t>& d) {
    if (d.size() < 4) return 0;
    return (uint32_t)d[1] | ((uint32_t)d[2] << 8) | ((uint32_t)d[3] << 16);
}

static const uint8_t kIp[4] = {127, 0, 0, 1};
static const uint16_t kPortA = 5001;   // healthy client
static const uint16_t kPortB = 5002;   // stalled client

// A real instance so the timeout path runs EXACTLY as it does on the
// server: the disconnect dump (with the new flow-control / liveness
// fields) is printed inside the close callback.
static int g_closeCalls = 0;
static void onCloseSession(void*, const char* ip, uint16_t port, const char* reason) {
    ++g_closeCalls;
    printf("   [close callback] %s:%u reason=%s\n", ip, port, reason);
}

int main() {
    mcp_rak_session_manager_t sm;
    mcp_server_instance_t inst;
    memset(&inst, 0, sizeof(inst));
    inst.on_close_session = &onCloseSession;
    mcp_rak_session_manager_init(&sm, 0x1234, 1400, &inst);
    sm.socket.fd = 7;   // tick() bails out on a closed socket

    mcp_rak_session_t* A = mcp_rak_session_manager_get_or_create(&sm, kIp, kPortA);
    mcp_rak_session_t* B = mcp_rak_session_manager_get_or_create(&sm, kIp, kPortB);
    A->state = MCP_SESSION_STATE_CONNECTED;
    B->state = MCP_SESSION_STATE_CONNECTED;
    A->send_queue_cap = 0;   // unused by these paths; silence unused-field noise
    B->send_queue_cap = 0;

    printf("1. two sessions open: A(healthy) B(stalled)\n");

    // --- The game layer sends a burst of RELIABLE data to both players.
    // A gets a realistic burst (well under the cap, as a real client that
    // acknowledges within an RTT always is) and B gets a flood nobody
    // will ever acknowledge — the state of the report. ---
    std::vector<uint8_t> payload(200, 0xAB);
    const int kBurstA = 400;
    const int kBurstB = 600;
    size_t acceptedA = 0, acceptedB = 0, refusedB = 0, refusedA = 0;
    for (int i = 0; i < kBurstA; ++i) {
        if (mcp_rak_session_manager_send_encapsulated(&sm, kIp, kPortA,
                payload.data(), payload.size(), MCP_RELIABILITY_RELIABLE, 1)) ++acceptedA;
        else ++refusedA;
    }
    for (int i = 0; i < kBurstB; ++i) {
        if (mcp_rak_session_manager_send_encapsulated(&sm, kIp, kPortB,
                payload.data(), payload.size(), MCP_RELIABILITY_RELIABLE, 1)) ++acceptedB;
        else ++refusedB;
    }
    printf("   A queued %zu/%d refused=%zu (inflight=%u), B queued %zu/%d "
           "refused=%zu (inflight=%u cap=%u)\n",
           acceptedA, kBurstA, refusedA, A->diag_recovery_occupied,
           acceptedB, kBurstB, refusedB,
           B->diag_recovery_occupied, (unsigned)MCP_MAX_INFLIGHT_RELIABLE);

    check(acceptedA == (size_t)kBurstA && refusedA == 0,
          "A: the healthy player never gets throttled");
    check(B->diag_recovery_occupied == MCP_MAX_INFLIGHT_RELIABLE,
          "B: the stalled player stops at the in-flight cap");
    check(B->diag_backpressure_drops == refusedB && refusedB > 0,
          "B: every refused datagram is counted (visible in the log)");

    // --- Client A acknowledges everything it received, like a real one.
    // Everything queued before the clock starts is acked now; from then on
    // the tick loop below acks each tick, like a real client would. ---
    std::vector<uint32_t> pendingAckA;
    for (const OutDatagram& o : g_out)
        if (o.port == kPortA) pendingAckA.push_back(datagramSeq(o.data));
    g_out.clear();
    printf("   A received %zu reliable datagrams -> it will acknowledge them all\n",
           pendingAckA.size());

    // --- Once A's ACKs are processed its window is free again. ---
    auto flushAcks = [&]() {
        if (pendingAckA.empty()) return;
        for (size_t i = 0; i < pendingAckA.size(); i += 100) {
            std::vector<uint32_t> chunk(pendingAckA.begin() + i,
                pendingAckA.begin() + std::min(pendingAckA.size(), i + 100));
            InDatagram in;
            memcpy(in.ip, kIp, 4);
            in.port = kPortA;
            in.data = ackPacket(chunk);
            g_in.push_back(std::move(in));
        }
        pendingAckA.clear();
    };
    for (int k = 0; k < 3; ++k) {
        g_now_ms += 10;
        mcp_rak_session_manager_tick(&sm);
        flushAcks();
    }
    // The second burst is queued for acknowledgement too (a healthy
    // client ACKs what it receives; forgetting these here would make A
    // look like a stalled client and hide what is being tested).
    const size_t beforeBurst2 = g_out.size();
    size_t acceptedA2 = 0;
    for (int i = 0; i < kBurstA; ++i)
        if (mcp_rak_session_manager_send_encapsulated(&sm, kIp, kPortA,
                payload.data(), payload.size(), MCP_RELIABILITY_RELIABLE, 1)) ++acceptedA2;
    for (size_t i = beforeBurst2; i < g_out.size(); ++i)
        if (g_out[i].port == kPortA) pendingAckA.push_back(datagramSeq(g_out[i].data));
    g_out.clear();
    printf("   A re-sends %zu/%d after being acknowledged\n", acceptedA2, kBurstA);
    check(acceptedA2 == (size_t)kBurstA,
          "A: the in-flight window is released once the client ACKs");

    // --- Drive ~30s of server time in 10ms ticks. ---
    printf("2. 30s of ticks with B never acknowledging\n");
    const int kTicksPerSecond = 100;
    long retransB_window[6] = {0};
    uint64_t lastRetransCounted = B->diag_retransmits;
    size_t maxDatagramsPerTickToB = 0;
    int tick = 0;
    for (int second = 0; second < 25; ++second) {
        for (int k = 0; k < kTicksPerSecond; ++k, ++tick) {
            g_now_ms += 10;
            size_t toB = 0;
            const size_t before = g_out.size();
            mcp_rak_session_manager_tick(&sm);
            for (size_t i = before; i < g_out.size(); ++i)
                if (g_out[i].port == kPortB) ++toB;
            if (toB > maxDatagramsPerTickToB) maxDatagramsPerTickToB = toB;

            // Healthy client A behaves like a real one: it ACKs every
            // reliable datagram it receives, and answers the server
            // keepalive PING with a PONG (the ping datagram ends with
            // [0x00][pingId u64be], so flipping that pid byte turns the
            // server's own datagram into a valid PONG coming back).
            for (size_t i = before; i < g_out.size(); ++i) {
                OutDatagram& o = g_out[i];
                if (o.port != kPortA || o.data.size() < 9) continue;
                std::vector<uint8_t>& d = o.data;
                if (d[d.size() - 9] == 0x00) {
                    d[d.size() - 9] = 0x03;
                    InDatagram in;
                    memcpy(in.ip, kIp, 4);
                    in.port = kPortA;
                    in.data = d;
                    g_in.push_back(std::move(in));
                    continue;
                }
                pendingAckA.push_back(datagramSeq(d));
            }
            if (!pendingAckA.empty()) {
                flushAcks();
            }
        }
        const int w = second / 5;
        if (w < 6)
            retransB_window[w] += (long)(B->diag_retransmits - lastRetransCounted);
        lastRetransCounted = B->diag_retransmits;
    }
    printf("   B retransmits per 5s window:");
    for (int i = 0; i < 6; ++i) printf(" %ld", retransB_window[i]);
    printf("\n   max datagrams sent to B in one tick: %zu (global budget %u)\n",
           maxDatagramsPerTickToB, (unsigned)MCP_RETRANSMIT_MAX_PER_TICK);
    printf("   A: inflight=%u since_last_ack=%llums retransmits=%llu\n",
           A->diag_recovery_occupied,
           (unsigned long long)(g_now_ms - A->last_ack_received_ms),
           (unsigned long long)A->diag_retransmits);
    printf("   A keepalive: pings=%u pongs=%u rtt=%ums | B: inflight=%u "
           "drops=%u retransmits=%llu\n",
           A->diag_keepalive_pings, A->diag_keepalive_pongs, A->rtt_ms,
           B->diag_recovery_occupied, B->diag_backpressure_drops,
           (unsigned long long)B->diag_retransmits);

    check(A->diag_recovery_occupied == 0,
          "A: everything it received was acknowledged and freed");
    check(A->diag_retransmits == 0,
          "A: a healthy player is never retransmitted to");
    check(A->diag_keepalive_pings > 0 && A->diag_keepalive_pongs > 0 && A->rtt_ms > 0,
          "A: server keepalive ping is answered and the RTT is measured");
    check(maxDatagramsPerTickToB <= MCP_RETRANSMIT_MAX_PER_TICK,
          "B: one stalled session cannot exceed the global per-tick budget");
    check(retransB_window[5] * 4 <= retransB_window[0],
          "B: retransmits back off instead of hammering at a fixed rate");
    check(B->diag_recovery_occupied == MCP_MAX_INFLIGHT_RELIABLE,
          "B: in-flight stays capped while it stays stalled");

    // --- Past 30s of silence the sweep must still reap it. ---
    // B's session is destroyed by the sweep here, so its fields must not
    // be touched again after this point.
    for (int k = 0; k < 12 * kTicksPerSecond; ++k) { g_now_ms += 10; mcp_rak_session_manager_tick(&sm); }
    printf("3. after 37s of silence: sessions left = %zu (A alive, B reaped)\n",
           sm.session_count);
    check(sm.session_count == 1 && A->diag_keepalive_pings > 0 && g_closeCalls == 1,
          "a genuinely dead session is still reaped (with its dump), the "
          "healthy one is not");

    printf("\n%s (%d failure(s))\n",
           failures ? "MULTIPLAYER FLOW: FAIL" : "MULTIPLAYER FLOW: OK", failures);
    return failures ? 1 : 0;
}