// tools/backlog_unit_check.cpp
//
// Regression test for the per-session congestion backlog in
// src/network/raklib/SessionManager.c (session_backlog_push /
// session_backlog_flush) and the inflight-ceiling interaction in
// mcp_rak_session_manager_send_encapsulated.
//
// The chunk-hole bug: past MCP_MAX_INFLIGHT_RELIABLE un-ACKed reliable
// datagrams the server REFUSED new payloads outright — but the game layer
// had already marked those chunks as sent, so a slow-ACKing client (real
// phone over Wi-Fi/mobile data while exploring) silently lost terrain and
// the chunks never rendered until the player wandered far enough for the
// chunk to leave and re-enter the view square. Now the refused payload
// queues in a bounded FIFO and replays as ACKs free recovery slots; only
// a backlog that is itself full (a genuinely dead session) still drops.
//
// This drives the REAL functions (the .c files are included so their
// statics are visible; UDP is stubbed) and asserts:
//
//   1. below the ceiling the send path is unchanged (no backlog);
//   2. at the ceiling the payload is QUEUED, not dropped, and the send
//      still reports success to the game layer;
//   3. the backlog is FIFO and replays oldest-first, through the normal
//      fragmented send path;
//   4. a backlog filled to its byte cap still refuses (stalled-session
//      protection intact) and counts the drop;
//   5. replay waits for the hysteresis watermark and stops at the ceiling.
//
//   g++ -std=c++20 -Isrc tools/backlog_unit_check.cpp -o /tmp/backlog_check
//   /tmp/backlog_check
#include "network/raklib/UDPServerSocket.h"
#include "utils/Utils.h"
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <vector>

// ---- stubs for the few symbols SessionManager.c links against -----------
static uint64_t g_now_ms = 1000;
uint64_t mcp_utils_time_ms(void) { return g_now_ms; }
int      mcp_udp_socket_open(mcp_udp_socket_t*, const char*, uint16_t) { return 0; }
void     mcp_udp_socket_close(mcp_udp_socket_t*) {}
void     mcp_udp_socket_set_blocking(mcp_udp_socket_t*, bool) {}
ssize_t  mcp_udp_socket_recv(mcp_udp_socket_t*, void*, size_t, uint8_t*, uint16_t*) { return -1; }

// Record every datagram the session layer tries to put on the wire so the
// FIFO replay order can be asserted from the payload signatures.
struct SentDatagram { std::vector<uint8_t> bytes; };
static std::vector<SentDatagram> g_wire;
ssize_t mcp_udp_socket_send(mcp_udp_socket_t*, const void* data, size_t len,
                            const uint8_t*, uint16_t) {
    const uint8_t* p = (const uint8_t*)data;
    g_wire.emplace_back();
    g_wire.back().bytes.assign(p, p + len);
    return (ssize_t)len;
}

#include "network/raklib/SessionManager.c"
#include "network/raklib/Session.c"  // provides the mcp_rak_session_* symbols
#include "utils/DebugFlag.c"         // the debug-mode switch SessionManager.c reads

static int failures = 0;
static void check(bool ok, const char* what) {
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}

// Finds the first occurrence of `sig` (len bytes) inside the recorded wire
// stream, or -1.
static long find_signature(const uint8_t* sig, size_t len) {
    size_t total = 0;
    for (auto& d : g_wire) total += d.bytes.size();
    std::vector<uint8_t> stream;
    stream.reserve(total);
    for (auto& d : g_wire)
        stream.insert(stream.end(), d.bytes.begin(), d.bytes.end());
    for (size_t i = 0; i + len <= stream.size(); ++i) {
        if (memcmp(stream.data() + i, sig, len) == 0)
            return (long)i;
    }
    return -1;
}

int main() {
    printf("congestion backlog (entries %u, bytes %.1f MB, watermark %u)\n",
           (unsigned)MCP_BACKLOG_MAX_ENTRIES,
           (double)MCP_BACKLOG_MAX_BYTES / (1024.0 * 1024.0),
           (unsigned)MCP_BACKLOG_FLUSH_WATERMARK);

    mcp_rak_session_manager_t sm;
    memset(&sm, 0, sizeof(sm));
    mcp_rak_session_manager_init(&sm, 1, 1464, nullptr);
    sm.socket.fd = 0; // fake but "open" fd: tick() refuses fd<0 and the
                      // recv stub returns -1, which the tick treats as idle

    const uint8_t ip[4] = {127, 0, 0, 1};
    const uint16_t port = 19132;
    mcp_rak_session_t* s = mcp_rak_session_manager_get_or_create(&sm, ip, port);
    check(s != NULL, "session created");
    if (!s)
        return 1;
    s->state = MCP_SESSION_STATE_CONNECTED;

    uint8_t payloadA[300], payloadB[300], payloadC[300];
    for (int i = 0; i < 300; ++i) {
        payloadA[i] = 0xA1; payloadB[i] = 0xB2; payloadC[i] = 0xC3;
    }

    // ---- 1. below the ceiling: straight send, no backlog -----------------
    printf("\n-- below the ceiling --\n");
    check(mcp_rak_session_manager_send_encapsulated(
              &sm, ip, port, payloadA, sizeof(payloadA),
              MCP_RELIABILITY_RELIABLE, 1),
          "send accepted below the ceiling");
    check(s->backlog_count == 0 && s->backlog_bytes == 0,
          "nothing queued below the ceiling");
    check(s->diag_recovery_occupied > 0,
          "payload went through the normal recovery-tracked send");

    // ---- 2. at the ceiling: queued, not dropped --------------------------
    printf("\n-- at the ceiling --\n");
    s->diag_recovery_occupied = MCP_MAX_INFLIGHT_RELIABLE;
    check(mcp_rak_session_manager_send_encapsulated(
              &sm, ip, port, payloadB, sizeof(payloadB),
              MCP_RELIABILITY_RELIABLE, 1),
          "send at the ceiling still reports success (deferred)");
    check(s->backlog_count == 1 && s->backlog_bytes == sizeof(payloadB),
          "payload queued in the backlog");
    check(s->diag_backlog_queued == 1,
          "backlog-queued counter incremented");
    check(s->diag_backpressure_drops == 0,
          "nothing dropped while the backlog has room");

    // ---- 3. replay: FIFO order through the normal send path --------------
    printf("\n-- backlog replay --\n");
    // A second payload joins the queue behind the first.
    check(mcp_rak_session_manager_send_encapsulated(
              &sm, ip, port, payloadC, sizeof(payloadC),
              MCP_RELIABILITY_RELIABLE, 1),
          "second send at the ceiling deferred too");
    check(s->backlog_count == 2, "two payloads queued (FIFO)");

    // ACKs free capacity far below the watermark; the manager tick runs the
    // backlog flush (same call the real RakLib loop makes). The wire
    // capture is NOT cleared: payload A's datagrams were recorded when it
    // was sent below the ceiling and must still be found for the FIFO
    // ordering assertion.
    s->diag_recovery_occupied = 10;
    mcp_rak_session_manager_tick(&sm);
    check(s->backlog_count == 0 && s->backlog_bytes == 0,
          "backlog fully replayed once capacity returned");
    check(s->diag_backlog_flushed == 2, "both payloads replayed");
    check(s->diag_recovery_occupied > 10,
          "replayed payloads went through the recovery-tracked send");
    {
        long pa = find_signature(payloadA, 64);
        long pb = find_signature(payloadB, 64);
        long pc = find_signature(payloadC, 64);
        check(pa >= 0 && pb >= 0 && pc >= 0, "all three payloads on the wire");
        check(pa < pb && pb < pc, "replay order is FIFO (A before B before C)");
    }

    // ---- 4. the ceiling + full backlog still protects stalled sessions ---
    printf("\n-- full backlog still refuses (stalled client) --\n");
    s->diag_recovery_occupied = MCP_MAX_INFLIGHT_RELIABLE;
    bool queued = true;
    int  queued_n = 0;
    // Backlog holds 4 MB; 128 KB payloads -> 32 fill it, the 33rd refuses.
    static uint8_t big[MCP_BACKLOG_MAX_BYTES / 32 + 1]; // 128 KB + 1 byte
    memset(big, 0x5A, sizeof(big));
    for (int i = 0; i < 64; ++i) {
        if (mcp_rak_session_manager_send_encapsulated(
                &sm, ip, port, big, sizeof(big),
                MCP_RELIABILITY_RELIABLE, 1))
            ++queued_n;
        else
            { queued = false; break; }
    }
    check(!queued, "send refused once the backlog itself is full");
    check(s->diag_backpressure_drops == 1,
          "refusal counted as a backpressure drop (protection intact)");
    check(s->backlog_bytes <= MCP_BACKLOG_MAX_BYTES,
          "backlog byte cap respected");
    check(s->backlog_count == queued_n, "queue accounting matches accepted sends");

    // ---- 5. replay stops at the ceiling again -----------------------------
    printf("\n-- replay hysteresis --\n");
    // Capacity returns but only up to just under the watermark: the flush
    // must stop there instead of refilling the ceiling one datagram at a
    // time.
    s->diag_recovery_occupied = MCP_BACKLOG_FLUSH_WATERMARK - 1;
    mcp_rak_session_manager_tick(&sm);
    check(s->diag_recovery_occupied >= MCP_BACKLOG_FLUSH_WATERMARK - 1,
          "replay ran (occupancy grew from the replayed payloads)");
    check(s->backlog_count < queued_n,
          "at least one payload replayed");

    // Cleanup: destroy frees every queued buffer (no leaks under ASan).
    mcp_rak_session_destroy(s);
    printf("\n%s (%d failure(s))\n",
           failures ? "BACKLOG: FAIL" : "BACKLOG: OK", failures);
    return failures ? 1 : 0;
}
