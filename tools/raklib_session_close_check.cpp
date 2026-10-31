// tools/raklib_session_close_check.cpp
//
// Unit test for the RakLib UAF hotfix: the new public teardown primitive
// mcp_rak_session_manager_close() (src/network/raklib/SessionManager.{h,cpp})
// and the delegating mcp_server_handler_close_session() in ServerHandler.cpp.
//
// The UAF: the kick path (ServerHandler) freed the session with
// destroy+free+swap-remove but NEVER erased the O(1) endpoint-index entry,
// so map[key] kept a dangling pointer that the next ACK/OCR1 datagram from
// that endpoint dereferenced (handle_ack READ, OCR1 WRITE). The hotfix
// consolidates teardown in the owner: erase exact (key, ptr) from the index,
// destroy, free, swap-remove — located by scanning the owner array, never
// the index.
//
// These checks drive the REAL SessionManager/Session/ServerHandler code
// (real TUs linked below) and assert the invariant that used to break:
// after EVERY close, find(ep)==NULL and the number of non-null index
// entries equals session_count. No bind()/socket needed: the manager is
// initialized with mcp_rak_session_manager_init() and socket.fd stays -1.
//
// Checks:
//   a) get_or_create of 3 endpoints; find() returns the same pointers;
//      session_count==3 and index entries==3.
//   b) close(A) -> true; find(A)==NULL; index entries==2==session_count.
//   c) close(A) again -> false (safe no-op).
//   d) close(unknown endpoint) -> false.
//   e) B and C survive with their ORIGINAL pointers and ip/port intact.
//   f) swap-remove: after closing sessions[0], sessions[0] is the old
//      last element (C) and find(C) still resolves to it.
//   g) anti-poisoning (S2): get_or_create(A) after the close yields a NEW
//      pointer (never the freed one); invariants hold; close again works.
//   h) 300-iteration create/close churn (ASan/LSan exercise); invariants
//      hold at every iteration; 0 sessions / 0 index entries at the end.
//   i) cap+realloc: 512 sessions fill MCP_SM_MAX_SESSIONS (513th -> NULL),
//      close 256 alternates, 256 more fit (513th live -> NULL), then close
//      all verifying invariants at every step; 0/0 at the end.
//   j) mcp_rak_session_manager_destroy() leaks nothing (LSan runs at exit
//      under the ASan build).
//
// Build & run (plain):
//   g++ -std=c++20 -O2 -Isrc -Isrc/network/raklib \
//       tools/raklib_session_close_check.cpp \
//       src/network/raklib/{SessionManager,Session,ServerHandler,UDPServerSocket,Binary,RakLib,RakLibServer}.cpp \
//       -o /tmp/raklib_close_check && /tmp/raklib_close_check
// Build & run (sanitized):
//   g++ -std=c++20 -fsanitize=address,undefined -fno-omit-frame-pointer -g -O1 \
//       ...same sources... -o /tmp/raklib_close_check_asan && /tmp/raklib_close_check_asan

#include "SessionManager.h"
#include "ServerHandler.h"
#include "utils/Utils.h" // declares mcp_utils_time_ms (C++ linkage)

#include <cstdio>
#include <cstring>
#include <cstdint>

// ---- stubs for the only external symbols the linked TUs need -------------
// (utils/Utils.cpp and utils/DebugFlag.cpp are NOT linked; the harness
// freezes the clock and runs with the debug flag off)

static uint64_t g_now_ms = 1000;
uint64_t mcp_utils_time_ms(void) { return g_now_ms; }

bool mcp_debug_enabled(void) { return false; }

// ---- tiny check framework (style of the other tools/*_check.cpp) ---------
static int g_checks = 0;
static int g_failed = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        ++g_checks;                                                        \
        if (!(cond)) {                                                     \
            ++g_failed;                                                    \
            std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);  \
        }                                                                  \
    } while (0)

// Count non-null entries in the O(1) endpoint index (the struct is public
// in SessionManager.h, so the test can audit the whole table).
static size_t map_entries(const mcp_rak_session_manager_t* sm) {
    size_t n = 0;
    for (size_t i = 0; i < MCP_SM_MAP_SIZE; ++i)
        if (sm->map_ptr[i])
            ++n;
    return n;
}

static bool ep_eq(const mcp_rak_session_t* s, uint8_t a, uint8_t b, uint8_t c, uint8_t d,
                  uint16_t port) {
    return s && s->ip[0] == a && s->ip[1] == b && s->ip[2] == c && s->ip[3] == d &&
           s->port == port;
}

static void make_ep(uint8_t* ip4, uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
    ip4[0] = a;
    ip4[1] = b;
    ip4[2] = c;
    ip4[3] = d;
}

int main() {
    static mcp_rak_session_manager_t sm;
    mcp_rak_session_manager_init(&sm, 0x1122334455667788LL, 1464, nullptr);
    CHECK(sm.socket.fd == -1); // no socket: close paths must not touch it

    const uint16_t live0_port = 0; // unused; keeps symmetry below

    // ---- (a) three endpoints, lookups hit the same pointers --------------
    uint8_t ipA[4], ipB[4], ipC[4];
    make_ep(ipA, 10, 0, 0, 1);
    make_ep(ipB, 10, 0, 0, 1);
    make_ep(ipC, 10, 0, 0, 2);
    mcp_rak_session_t* A = mcp_rak_session_manager_get_or_create(&sm, ipA, 1000);
    mcp_rak_session_t* B = mcp_rak_session_manager_get_or_create(&sm, ipB, 2000);
    mcp_rak_session_t* C = mcp_rak_session_manager_get_or_create(&sm, ipC, 1000);
    CHECK(A && B && C && A != B && B != C && A != C);
    CHECK(mcp_rak_session_manager_find(&sm, ipA, 1000) == A);
    CHECK(mcp_rak_session_manager_find(&sm, ipB, 2000) == B);
    CHECK(mcp_rak_session_manager_find(&sm, ipC, 1000) == C);
    CHECK(sm.session_count == 3);
    CHECK(map_entries(&sm) == 3);
    std::printf("a) create 3 + lookup identity: %s (count=%zu entries=%zu)\n",
                g_failed ? "FAILED" : "ok", sm.session_count, map_entries(&sm));

    // owner array order is creation order: A=0, B=1, C=2
    CHECK(sm.sessions[0] == A && sm.sessions[1] == B && sm.sessions[2] == C);

    // ---- (b) close(A): true, gone from index and array -------------------
    bool closed = mcp_rak_session_manager_close(&sm, ipA, 1000);
    CHECK(closed);
    CHECK(mcp_rak_session_manager_find(&sm, ipA, 1000) == nullptr);
    CHECK(sm.session_count == 2);
    CHECK(map_entries(&sm) == 2);
    CHECK(map_entries(&sm) == sm.session_count); // THE invariant of the fix
    std::printf("b) close(A): %s (count=%zu entries=%zu)\n", closed ? "ok" : "FAILED",
                sm.session_count, map_entries(&sm));

    // ---- (c) close(A) again: false, no crash -----------------------------
    bool again = mcp_rak_session_manager_close(&sm, ipA, 1000);
    CHECK(!again);
    CHECK(sm.session_count == 2 && map_entries(&sm) == 2);
    std::printf("c) double close(A): %s\n", !again ? "ok (false no-op)" : "FAILED");

    // ---- (d) close of a never-seen endpoint ------------------------------
    uint8_t ipX[4];
    make_ep(ipX, 99, 99, 99, 99);
    CHECK(!mcp_rak_session_manager_close(&sm, ipX, 1));
    CHECK(sm.session_count == 2 && map_entries(&sm) == 2);
    std::printf("d) close(unknown): ok (false)\n");

    // ---- (e) B and C untouched (pointers captured BEFORE the close) ------
    CHECK(mcp_rak_session_manager_find(&sm, ipB, 2000) == B);
    CHECK(mcp_rak_session_manager_find(&sm, ipC, 1000) == C);
    CHECK(ep_eq(B, 10, 0, 0, 1, 2000));
    CHECK(ep_eq(C, 10, 0, 0, 2, 1000));
    std::printf("e) B/C survive with original pointers+fields: %s\n",
                g_failed ? "FAILED" : "ok");

    // ---- (f) swap-remove: sessions[0] is now the old last (C) ------------
    CHECK(sm.session_count == 2);
    CHECK(sm.sessions[0] == C); // old sessions[2] swapped into the hole
    CHECK(sm.sessions[1] == B);
    CHECK(mcp_rak_session_manager_find(&sm, ipC, 1000) == C); // index still resolves
    std::printf("f) swap-remove compaction: %s\n", g_failed ? "FAILED" : "ok");

    // ---- (g) anti-poisoning: recreate A, never the freed block (S2) ------
    mcp_rak_session_t* A2 = mcp_rak_session_manager_get_or_create(&sm, ipA, 1000);
    CHECK(A2 != nullptr);
    CHECK(A2 != A); // a fresh allocation, not the stale (freed) pointer
    CHECK(mcp_rak_session_manager_find(&sm, ipA, 1000) == A2);
    CHECK(sm.session_count == 3 && map_entries(&sm) == 3);
    CHECK(ep_eq(A2, 10, 0, 0, 1, 1000));
    CHECK(mcp_rak_session_manager_close(&sm, ipA, 1000) == true);
    CHECK(mcp_rak_session_manager_find(&sm, ipA, 1000) == nullptr);
    CHECK(sm.session_count == 2 && map_entries(&sm) == 2);
    std::printf("g) recreate-after-close is a NEW session: %s\n",
                g_failed ? "FAILED" : "ok");

    // ---- (h) heavy create/close churn (300x) for ASan/LSan ---------------
    // First drain B and C (two more teardowns through the same path).
    CHECK(mcp_rak_session_manager_close(&sm, ipB, 2000) == true);
    CHECK(mcp_rak_session_manager_close(&sm, ipC, 1000) == true);
    CHECK(sm.session_count == 0 && map_entries(&sm) == 0);
    for (int i = 0; i < 300; ++i) {
        uint8_t ip[4] = {172, 16, 0, 1};
        uint16_t port = (uint16_t)(20000 + i);
        mcp_rak_session_t* s = mcp_rak_session_manager_get_or_create(&sm, ip, port);
        CHECK(s != nullptr);
        CHECK(mcp_rak_session_manager_find(&sm, ip, port) == s);
        CHECK(mcp_rak_session_manager_close(&sm, ip, port) == true);
        CHECK(mcp_rak_session_manager_find(&sm, ip, port) == nullptr);
        CHECK(map_entries(&sm) == sm.session_count);
        ++g_now_ms; // sessions are stamped with the clock at create
    }
    CHECK(sm.session_count == 0);
    CHECK(map_entries(&sm) == 0);
    std::printf("h) 300x churn: %s (final count=%zu entries=%zu)\n",
                g_failed ? "FAILED" : "ok", sm.session_count, map_entries(&sm));

    // ---- (i) cap + realloc + full drain ----------------------------------
    const uint16_t base1 = 30000;                    // 512 ports: 30000..30511
    uint8_t ipI[4] = {192, 168, 77, 7};
    size_t created = 0;
    mcp_rak_session_t* first = nullptr;
    for (int i = 0; i < (int)MCP_SM_MAX_SESSIONS; ++i) {
        mcp_rak_session_t* s =
            mcp_rak_session_manager_get_or_create(&sm, ipI, (uint16_t)(base1 + i));
        CHECK(s != nullptr);
        if (i == 0)
            first = s;
        ++created;
    }
    CHECK(created == MCP_SM_MAX_SESSIONS);
    CHECK(sm.session_count == MCP_SM_MAX_SESSIONS);
    CHECK(map_entries(&sm) == MCP_SM_MAX_SESSIONS);
    CHECK(sm.session_capacity >= MCP_SM_MAX_SESSIONS); // realloc grew the array
    // 513th live session must be refused by the cap
    CHECK(mcp_rak_session_manager_get_or_create(&sm, ipI, (uint16_t)(base1 + 600)) == nullptr);
    CHECK(sm.session_count == MCP_SM_MAX_SESSIONS);
    CHECK(map_entries(&sm) == MCP_SM_MAX_SESSIONS);

    // close the even-indexed 256; odd ones must survive with same pointers
    size_t closed_even = 0;
    for (int i = 0; i < (int)MCP_SM_MAX_SESSIONS; i += 2) {
        uint16_t p = (uint16_t)(base1 + i);
        CHECK(mcp_rak_session_manager_close(&sm, ipI, p) == true);
        CHECK(mcp_rak_session_manager_find(&sm, ipI, p) == nullptr);
        ++closed_even;
        CHECK(map_entries(&sm) == sm.session_count);
    }
    CHECK(closed_even == 256);
    CHECK(sm.session_count == 256);
    CHECK(map_entries(&sm) == 256);
    for (int i = 1; i < (int)MCP_SM_MAX_SESSIONS; i += 2) {
        uint16_t p = (uint16_t)(base1 + i);
        mcp_rak_session_t* s = mcp_rak_session_manager_find(&sm, ipI, p);
        CHECK(s != nullptr);
        CHECK(s->port == p); // live sessions keep resolving and intact
    }

    // 256 more must fit (256 live + 256 == cap); the 257th (513th live) fails
    const uint16_t base2 = 30512; // ports 30512..30771
    size_t second_wave = 0;
    for (int i = 0; i < 260; ++i) {
        mcp_rak_session_t* s =
            mcp_rak_session_manager_get_or_create(&sm, ipI, (uint16_t)(base2 + i));
        if (s)
            ++second_wave;
        CHECK(map_entries(&sm) == sm.session_count);
    }
    CHECK(second_wave == 256); // 256 fit, the remaining 4 hit the cap
    CHECK(sm.session_count == MCP_SM_MAX_SESSIONS);
    CHECK(map_entries(&sm) == MCP_SM_MAX_SESSIONS);
    CHECK(mcp_rak_session_manager_get_or_create(&sm, ipI, 55555) == nullptr);

    // drain EVERYTHING one by one, verifying invariants at each step
    size_t drained = 0;
    for (int i = 1; i < (int)MCP_SM_MAX_SESSIONS; i += 2) { // odd leftovers
        uint16_t p = (uint16_t)(base1 + i);
        CHECK(mcp_rak_session_manager_close(&sm, ipI, p) == true);
        ++drained;
        CHECK(map_entries(&sm) == sm.session_count);
    }
    for (int i = 0; i < 256; ++i) { // second wave
        uint16_t p = (uint16_t)(base2 + i);
        CHECK(mcp_rak_session_manager_close(&sm, ipI, p) == true);
        ++drained;
        CHECK(map_entries(&sm) == sm.session_count);
    }
    CHECK(drained == 512);
    CHECK(sm.session_count == 0);
    CHECK(map_entries(&sm) == 0);
    // the very first session's endpoint must be free again (block recycled
    // many times over; recreated pointer must be new and valid)
    mcp_rak_session_t* re = mcp_rak_session_manager_get_or_create(&sm, ipI, base1);
    CHECK(re != nullptr && re != first);
    CHECK(mcp_rak_session_manager_close(&sm, ipI, base1) == true);
    CHECK(sm.session_count == 0 && map_entries(&sm) == 0);
    std::printf("i) cap/realloc/drain 512: %s (final count=%zu entries=%zu)\n",
                g_failed ? "FAILED" : "ok", sm.session_count, map_entries(&sm));

    // ---- (j) destroy: frees everything; LSan verdict at exit -------------
    // But first: the production kick entry point. ServerHandler used to
    // implement its own free WITHOUT the index erase (the UAF); it must
    // now delegate to the manager and leave the same invariants.
    {
        uint8_t ipH[4] = {10, 20, 30, 40};
        mcp_rak_session_t* H = mcp_rak_session_manager_get_or_create(&sm, ipH, 7777);
        CHECK(H != nullptr);
        mcp_server_handler_t handler;
        mcp_server_handler_init(&handler, &sm);
        mcp_server_handler_close_session(&handler, ipH, 7777); // the kick path
        CHECK(mcp_rak_session_manager_find(&sm, ipH, 7777) == nullptr);
        CHECK(sm.session_count == 0 && map_entries(&sm) == 0);
        mcp_server_handler_close_session(&handler, ipH, 7777); // repeat: safe no-op
        CHECK(sm.session_count == 0 && map_entries(&sm) == 0);
        std::printf("j2) ServerHandler.close_session delegates with invariants: %s\n",
                    g_failed ? "FAILED" : "ok");
    }
    mcp_rak_session_manager_destroy(&sm);
    std::printf("j) manager_destroy: ok\n");

    std::printf("\nchecks run: %d\n", g_checks);
    if (g_failed) {
        std::printf("FAILED: %d check(s)\n", g_failed);
        return 1;
    }
    std::printf("ALL PASSED\n");
    return 0;
}
