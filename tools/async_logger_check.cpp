// tools/async_logger_check.cpp
//
// Regression check for issue #8 (portable AsyncMainLogger): after moving
// the logger off pthread, it must still start its worker, flush without
// waiting for shutdown, preserve push order, and drain the ring during
// destroy instead of dropping messages.
//
// The worker writes its output to stdout, so stdout is discarded below
// and every result line is printed to stderr.
//
//   tools/run_async_logger_check.sh
#include "logger/AsyncMainLogger.h"

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <fstream>
#include <string>
#include <thread>

namespace {

int g_failures = 0;

void check(bool ok, const char* what) {
    std::fprintf(stderr, "  [%s] %s\n", ok ? " OK " : "FAIL", what);
    if (!ok) ++g_failures;
}

}  // namespace

int main() {
    static_assert(offsetof(mcp_async_logger_t, vtable) == 0,
                  "vtable must stay the first field (mcp_main_logger_t layout)");

    const char* path = "async_logger_check.log";
    std::remove(path);

    // Discard the logger's stdout chatter; all results go to stderr.
#ifdef _WIN32
    bool silenced = std::freopen("NUL", "w", stdout) != nullptr;
#else
    bool silenced = std::freopen("/dev/null", "w", stdout) != nullptr;
#endif
    (void)silenced;  // losing the logger's chatter is acceptable

    mcp_main_logger_async_init(path, false);
    check(mcp_async_logger_is_initialized(), "init spawns the worker thread");

    mcp_async_logger_t* lg = mcp_async_logger_get_instance();
    mcp_logger_t* v = &lg->vtable;

    const int kBurst = 2000;  // flushed while the logger is running
    const int kTail  = 200;   // pushed just before destroy: shutdown drain
    const int kTotal = kBurst + kTail;

    for (int i = 0; i < kBurst; ++i) {
        char msg[32];
        std::snprintf(msg, sizeof msg, "MSG-%d", i);
        v->info(v->impl, msg);
    }

    // The worker must flush on its own — destroy() has not been called yet.
    std::this_thread::sleep_for(std::chrono::milliseconds(800));
    int flushed_before_shutdown = 0;
    {
        std::ifstream in(path);
        std::string line;
        while (std::getline(in, line)) ++flushed_before_shutdown;
    }
    check(flushed_before_shutdown > 0, "worker flushes asynchronously before shutdown");

    // Burst without waiting, then destroy: shutdown must drain, not drop.
    for (int i = kBurst; i < kTotal; ++i) {
        char msg[32];
        std::snprintf(msg, sizeof msg, "MSG-%d", i);
        v->info(v->impl, msg);
    }
    mcp_main_logger_async_destroy();
    check(!mcp_async_logger_is_initialized(), "destroy joins the worker");

    // Every message must reach the file, in push order, exactly once.
    int next_expected = 0;
    int out_of_order = 0;
    {
        std::ifstream in(path);
        std::string line;
        while (std::getline(in, line)) {
            const char* p = std::strstr(line.c_str(), "MSG-");
            if (!p) continue;
            int n = std::atoi(p + 4);
            if (n == next_expected) {
                ++next_expected;
            } else {
                ++out_of_order;  // missing, duplicated or reordered message
            }
        }
    }

    char what[96];
    std::snprintf(what, sizeof what, "all %d messages in push order (got %d, %d wrong)",
                  kTotal, next_expected, out_of_order);
    check(next_expected == kTotal && out_of_order == 0, what);
    check(lg->dropped_count == 0, "ring never overflowed (dropped_count == 0)");

    std::fprintf(stderr, "%s (%d failure(s))\n",
                 g_failures ? "ASYNC LOGGER CHECK: FAIL" : "ASYNC LOGGER CHECK: OK",
                 g_failures);
    std::remove(path);
    return g_failures ? 1 : 0;
}
