// tools/reliable_dup_check.cpp
//
// Regression test for the reliable-message duplicate table in
// src/network/raklib/SessionManager.c (reliable_index_accept /
// reliable_remember / reliable_already_delivered).
//
// The payload-aware duplicate table used to be a 256-slot RING while the
// receive window (reliable_recv_window) holds MCP_RELIABLE_WINDOW_SIZE
// (2048) entries. A RETRANSMIT that reused a message index already
// delivered more than 256 messages earlier still found its bit set in the
// window, but its payload had long since been evicted from the ring — so
// reliable_already_delivered() returned false and the message was handed to
// the game layer a SECOND time (duplicated actions / entity state).
//
// That failure needs real retransmission to trigger, which is why it is
// invisible on a lossless LAN/loopback and only shows up on a hosted server
// over the Internet. The table is now direct-mapped by
// `message_index % MCP_RELIABLE_WINDOW_SIZE`, i.e. exactly the same index
// space as the window, so the entry can never be evicted while its window
// bit is still set.
//
// This drives the REAL functions (the .c is included so its statics are
// visible) and asserts the fix, plus reproduces the OLD 256-ring behaviour
// to prove the scenario is not vacuous.
//
//   g++ -std=c++20 -Isrc tools/reliable_dup_check.cpp -o /tmp/reliable_dup
//   /tmp/reliable_dup
#include "network/raklib/UDPServerSocket.h"
#include "utils/Utils.h"
#include <cstdio>
#include <cstring>
#include <cstdint>

// ---- stubs for the few symbols SessionManager.c links against -----------
uint64_t mcp_utils_time_ms(void) { return 0; }
int      mcp_udp_socket_open(mcp_udp_socket_t*, const char*, uint16_t) { return 0; }
void     mcp_udp_socket_close(mcp_udp_socket_t*) {}
void     mcp_udp_socket_set_blocking(mcp_udp_socket_t*, bool) {}
ssize_t  mcp_udp_socket_recv(mcp_udp_socket_t*, void*, size_t, uint8_t*, uint16_t*) { return -1; }
ssize_t  mcp_udp_socket_send(mcp_udp_socket_t*, const void*, size_t, const uint8_t*, uint16_t) { return 0; }

#include "network/raklib/SessionManager.c"
#include "network/raklib/Session.c"     // provides the mcp_rak_session_* symbols SessionManager.c calls

static int failures = 0;
static void check(bool ok, const char* what) {
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}

// ---------------------------------------------------------------------------
// Faithful model of the OLD 256-slot ring, to show the scenario is real:
// it must FAIL to recognize the same retransmit the shipped code now drops.
// ---------------------------------------------------------------------------
struct legacy_seen { uint32_t index, hash, len; bool used; };
#define LEGACY_SLOTS 256
static legacy_seen g_legacy[LEGACY_SLOTS];
static uint32_t    g_legacy_next = 0;

static uint32_t fnv1a(const uint8_t* p, size_t n) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 16777619u; }
    return h;
}
static void legacy_remember(uint32_t index, const uint8_t* p, size_t n) {
    uint32_t i = g_legacy_next++ % LEGACY_SLOTS;
    g_legacy[i].index = index; g_legacy[i].hash = fnv1a(p, n);
    g_legacy[i].len = (uint32_t)n; g_legacy[i].used = true;
}
static bool legacy_already_delivered(uint32_t index, const uint8_t* p, size_t n) {
    uint32_t h = fnv1a(p, n);
    for (uint32_t i = 0; i < LEGACY_SLOTS; ++i) {
        if (!g_legacy[i].used) continue;
        if (g_legacy[i].index != index) continue;
        if (g_legacy[i].hash == h && g_legacy[i].len == (uint32_t)n) return true;
    }
    return false;
}

int main() {
    printf("reliable duplicate window (%u slots, window %u)\n",
           (unsigned)MCP_RELIABLE_WINDOW_SIZE, (unsigned)MCP_RELIABLE_WINDOW_SIZE);

    // A 9-byte reliable message (the shape seen in the hosting log:
    // "[NET] Duplicate reliable message dropped (index=..., 9 bytes)").
    uint8_t first[9];
    for (int i = 0; i < 9; ++i) first[i] = (uint8_t)(0x40 + i);

    // ---- 1. drive the real code ------------------------------------------
    printf("\n-- shipped table --\n");
    mcp_rak_session_t s;
    memset(&s, 0, sizeof(s));
    s.last_reliable_index = -1;   // nothing received yet (see init in Session.c)

    check(reliable_index_accept(&s, 0, first, 9), "index 0 accepted on first receipt");

    bool all = true;
    for (uint32_t mi = 1; mi <= 1000; ++mi) {
        uint8_t p[9];
        memcpy(p, first, 9);
        p[0] = (uint8_t)(mi & 0xFF);
        if (!reliable_index_accept(&s, mi, p, 9)) all = false;
    }
    check(all, "1000 fresh messages accepted");

    // The retransmit: SAME index (0), SAME bytes, 1000 messages later.
    check(!reliable_index_accept(&s, 0, first, 9),
          "retransmit of index 0 (1000 messages old) DROPPED");
    check(s.diag_duplicate_dropped == 1,
          "duplicate counter incremented (surfaced in the disconnect dump)");

    // Fresh, different bytes at a new index still go through.
    {
        uint8_t p[9]; memcpy(p, first, 9); p[0] = 0xAA;
        check(reliable_index_accept(&s, 1001, p, 9), "next fresh message accepted");
    }
    // A client that REUSES one index for different bytes is still delivered
    // (the LegacyBot case this table was added for) — must not regress.
    {
        uint8_t p[9]; memcpy(p, first, 9); p[0] = 0xBB;
        check(reliable_index_accept(&s, 1001, p, 9),
              "reused index with different bytes still accepted");
    }

    // ---- 2. the same scenario against the OLD ring ----------------------
    printf("\n-- old 256-slot ring (for contrast) --\n");
    legacy_remember(0, first, 9);
    for (uint32_t mi = 1; mi <= 1000; ++mi) {
        uint8_t p[9]; memcpy(p, first, 9); p[0] = (uint8_t)(mi & 0xFF);
        legacy_remember(mi, p, 9);
    }
    check(!legacy_already_delivered(0, first, 9),
          "old ring FORGETS index 0 -> the retransmit was re-delivered (the bug)");

    printf("\n%s (%d failure(s))\n",
           failures ? "RELIABLE DUP: FAIL" : "RELIABLE DUP: OK", failures);
    return failures ? 1 : 0;
}
