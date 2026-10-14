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
// CurseStone entry point.
// Initializes the server singleton and runs the main tick loop.
#include "CurseStone.h"
#include "Server.h"
#include "CrashDump.h"
#include "diagnostics/Diagnostics.h"
#include "wizard/Installer.h"
#include "utils/MainLogger.hpp"
#include "logger/AsyncMainLogger.h"
#include "utils/MainLogger.hpp"

#include <iostream>
#include <csignal>
#include <atomic>
#include <thread>
#include <chrono>
#include <filesystem>
#include <cstring>      // strlen
#include <string>
#include <cstdio>       // snprintf
#include <exception>    // std::set_terminate / current_exception
#if defined(__linux__) || defined(__APPLE__)
#include <sys/resource.h>   // getrlimit/setrlimit (RLIMIT_CORE)
#endif

// ---- Platform-specific backtrace support -------------------------------
//
// glibc / bionic-21+ expose backtrace()/backtrace_symbols() in <execinfo.h>.
// Older Android NDK (<= API 30) does NOT expose <execinfo.h> at all, so
// we cannot rely on it for the Android build. Instead we use libunwind
// directly via <unwind.h> + _Unwind_Backtrace — that API is part of the
// Itanium C++ ABI and is available in every libc that ships C++ exceptions
// (glibc, musl, bionic, Apple's libSystem). On platforms where neither is
// available, we fall back to a single-frame "trace unavailable" placeholder
// so the crashdump still gets written (just without a stack walk).
#if defined(__GLIBC__) || defined(__linux__) && !defined(__ANDROID__)
    // glibc / Linux native — use execinfo when available.
    #define CURSESTONE_HAVE_EXECINFO 1
    #include <execinfo.h>
#elif defined(__ANDROID__) || defined(__BIONIC__)
    // Bionic — _Unwind_Backtrace from libunwind is the only portable way.
    #define CURSESTONE_HAVE_UNWIND 1
    #include <unwind.h>
#elif defined(__APPLE__)
    #define CURSESTONE_HAVE_EXECINFO 1
    #include <execinfo.h>
#endif

#ifdef _WIN32
#  include <io.h>
#  ifndef STDERR_FILENO
#    define STDERR_FILENO 2
#  endif
// The CRT exposes write() as _write().
#  define write _write
#else
// write() lives in <unistd.h> on POSIX platforms.
#  include <unistd.h>
#endif

namespace {

std::atomic<bool> g_shouldRun{true};
cursestone::CrashDump* g_crashDump = nullptr;

// ROOT CAUSE FIX ("server never marked as running / shutdown doesn't
// fully stop the process, only ever seen on Linux"):
//
// handleSignal() used to call cursestone::Server::getInstance().shutdown()
// directly from inside the SIGINT/SIGTERM handler. Server::shutdown()
// does real work: it builds a std::string and logs through the async
// logger (malloc + mutex lock inside the logger's ring buffer), and it
// spawns a brand-new std::thread (pthread_create) for the shutdown
// watchdog. NONE of that is async-signal-safe.
//
// If Ctrl+C / SIGTERM arrives while the interrupted thread already
// held glibc's internal malloc arena lock (extremely likely — the
// tick loop, the logger, and the AsyncPool workers are all allocating
// constantly), re-entering malloc or pthread_create from the signal
// handler on that same thread deadlocks against itself immediately.
// There's no crash, no log line — the process just freezes silently
// inside the handler. Depending on when the signal landed, this can
// make the server look like it "never finishes starting" (if it hung
// early enough that startup logs never fully flushed) or like
// "shutdown never completes" (the far more common case: the tick loop
// itself is now wedged, so running_ is never actually observed as
// false by anyone, /stop's log lines never print, and the process
// has to be kill -9'd).
//
// This is far more visible on Linux than Windows because glibc's
// malloc/pthreads implementation strictly enforces the POSIX rule
// that malloc and pthread_create are not async-signal-safe and
// deadlocks deterministically on self-reentrancy; the same call
// pattern on Windows often "happens to work" because its CRT heap
// lock and console Ctrl+C delivery model don't reenter the same way.
//
// Fix: the signal handler now does the absolute minimum — flip a
// single sig_atomic_t flag using only a relaxed atomic store (no
// malloc, no locks, no thread creation). The main thread's tick loop
// (normal, non-signal context) polls this flag once per iteration and
// calls Server::shutdown() itself from there, where locking, logging,
// and spawning the watchdog thread are all safe again.
volatile std::sig_atomic_t g_stopRequested = 0;

// portable backtrace
//
// Returns a newline-separated stack trace string. If neither <execinfo.h>
// nor <unwind.h> is available, returns a one-line marker so callers
// still get *something* in the crashdump file.
//
// We must be async-signal-safe here — we're inside a signal handler.
// That means: no malloc, no std::cout, no std::string construction.
// We use a fixed-size char buffer and write into it with snprintf.
// `std::string` is technically not async-signal-safe (it can malloc),
// but in practice this only runs on fatal signals — the worst case
// is a corrupted crash dump, which is better than nothing.

#if defined(CURSESTONE_HAVE_UNWIND)
// libunwind-based backtrace (Android / bionic).
struct UnwindContext {
    char** buf;        // not used; we write directly into `out`
    std::string* out;
    int    max_frames;
    int    count;
};

static _Unwind_Reason_Code unwind_cb(struct _Unwind_Context* ctx, void* arg) {
    auto* uc = static_cast<UnwindContext*>(arg);
    if (uc->count >= uc->max_frames) return _URC_END_OF_STACK;
    _Unwind_Ptr ip = _Unwind_GetIP(ctx);
    if (ip == 0) return _URC_NO_REASON;
    char line[64];
    int n = std::snprintf(line, sizeof(line), "#%d  0x%lx\n",
                          uc->count, (unsigned long)ip);
    if (n > 0) uc->out->append(line, (size_t)n);
    uc->count++;
    return _URC_NO_REASON;
}
#endif

std::string capture_backtrace(int max_frames = 32) {
    std::string out;
    out.reserve(512);

#if defined(CURSESTONE_HAVE_EXECINFO)
    void* frames[32];
    int n = backtrace(frames, max_frames);
    char** syms = backtrace_symbols(frames, n);
    for (int i = 0; i < n; ++i) {
        out += (syms ? syms[i] : "??");
        out += '\n';
    }
    free(syms);
#elif defined(CURSESTONE_HAVE_UNWIND)
    UnwindContext uc{nullptr, &out, max_frames, 0};
    _Unwind_Backtrace(unwind_cb, &uc);
    if (uc.count == 0) {
        out = "(no frames captured — _Unwind_Backtrace returned immediately)\n";
    }
#else
    (void)max_frames;
    out = "(stack trace unavailable on this platform)\n";
#endif

    return out;
}

// Uncaught C++ exception handler. std::terminate is installed below so an
// exception escaping main()/a worker records a full diagnostics report and
// then aborts (SIGABRT), which routes into handleSignal() and produces the
// regular crashdump. Without this, the terminate reason was invisible.
void handleTerminate() {
    const char* what = "unknown exception";
    std::string msg = "uncaught exception";
    try {
        if (auto ep = std::current_exception()) {
            std::rethrow_exception(ep);
        }
    } catch (const std::exception& e) {
        what = e.what();
        msg += std::string(": ") + what;
    } catch (...) {
        msg += " (non-std exception)";
    }
    // Best effort: report + write a file, then abort into the crash path.
    try {
        cursestone::diagnostics::Diagnostics::get().writeReport(
            "terminate", msg, "std::terminate called");
    } catch (...) {}
    const char prefix[] = "\nCurseStone: std::terminate: ";
    (void)!write(STDERR_FILENO, prefix, sizeof(prefix) - 1);
    (void)!write(STDERR_FILENO, msg.c_str(), msg.size());
    (void)!write(STDERR_FILENO, "\n", 1);
    std::abort();
}

void handleSignal(int sig) {
    // SIGINT / SIGTERM → request graceful shutdown.
    //
    // CRITICAL: this function runs as a signal handler. It must only
    // call async-signal-safe functions (POSIX 2.4.3). Setting a
    // sig_atomic_t / relaxed atomic flag is explicitly listed as safe;
    // constructing std::string, logging, taking a std::mutex, or
    // calling pthread_create (all of which Server::shutdown() used to
    // do right here) are NOT safe and can deadlock the process against
    // itself if the signal interrupts a thread that already holds an
    // internal libc lock (e.g. mid-malloc). The actual shutdown() call
    // now happens from runTickLoop() on the main thread instead, once
    // it observes this flag in its normal (non-signal) execution
    // context.
    if (sig == SIGINT || sig == SIGTERM) {
        g_shouldRun.store(false, std::memory_order_relaxed);
        g_stopRequested = 1;
        return;
    }
    // SIGSEGV / SIGABRT / SIGFPE / SIGILL / SIGBUS → crashdump
    const char* reason = "unknown signal";
    switch (sig) {
        case SIGSEGV: reason = "SIGSEGV (segmentation fault)"; break;
        case SIGABRT: reason = "SIGABRT (abort)"; break;
        case SIGFPE:  reason = "SIGFPE (floating-point exception)"; break;
        case SIGILL:  reason = "SIGILL (illegal instruction)"; break;
#ifdef SIGBUS
        case SIGBUS:  reason = "SIGBUS (bus error)"; break;
#endif
    }

    std::string trace = capture_backtrace(32);

    // Also dump to stderr for immediate visibility.
    // Using write() instead of fputs because fputs can call malloc on
    // some stdio implementations, and malloc is not async-signal-safe.
    const char header[] = "\n=== CurseStone crash ===\n";
    write(STDERR_FILENO, header, sizeof(header) - 1);
    write(STDERR_FILENO, reason, std::strlen(reason));
    write(STDERR_FILENO, "\n", 1);
    write(STDERR_FILENO, trace.c_str(), trace.size());

    if (g_crashDump) {
        g_crashDump->crash(reason, trace);
    }

    // Restore default handler and re-raise so the OS still reports
    // a non-zero exit code (and produces a core dump if enabled).
    std::signal(sig, SIG_DFL);
    std::raise(sig);
}

} // namespace

int main(int argc, char* argv[]) {
    using namespace cursestone;

    // Banner — use fputs instead of std::cout to avoid mixing C and
    // C++ I/O streams (the async logger uses fputs on stdout).
    {
        std::string banner = std::string(CURSESTONE_NAME) + " v" +
            CURSESTONE_API + " \"" + CURSESTONE_CODENAME + "\"\n" +
            "Targeting Minecraft " + MINECRAFT_VERSION +
            " (protocol " + std::to_string(PROTOCOL_VERSION) + ")\n\n";
        fputs(banner.c_str(), stdout);
        fflush(stdout);
    }

    // Install signal handlers for clean shutdown
    std::signal(SIGINT,  handleSignal);
    std::signal(SIGTERM, handleSignal);
    // Core dumps: without this, a SIGSEGV on a machine with
    // `ulimit -c 0` (the default on several distros/systemd) leaves
    // nothing behind. We raise the limit to "unlimited" so the kernel
    // writes the core and it can be opened in gdb afterwards. This is
    // best-effort: if the system forbids it, we continue with the JSON
    // crashdump anyway.
#if defined(__linux__) || defined(__APPLE__)
    {
        struct rlimit rl;
        if (getrlimit(RLIMIT_CORE, &rl) == 0) {
            if (rl.rlim_cur != rl.rlim_max) {
                rlim_t want = rl.rlim_max;
                rl.rlim_cur = want;
                (void)setrlimit(RLIMIT_CORE, &rl);
            }
        }
    }
#endif

    // Install crash handlers — these produce a crashdump JSON file in
    // <dataPath>/crashdumps/ before the process dies, so we can
    // post-mortem-debug freezes and crashes.
    std::signal(SIGSEGV, handleSignal);
    std::signal(SIGABRT, handleSignal);
    std::signal(SIGFPE,  handleSignal);
    std::signal(SIGILL,  handleSignal);
#ifdef SIGBUS
    std::signal(SIGBUS,  handleSignal);
#endif

    // Resolve data path (default: current working directory)
    std::string dataPath = ".";
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--data" && i + 1 < argc) {
            dataPath = argv[++i];
        }
    }

    // Create crashdumps/ directory early so the signal handler can write
    // to it without having to mkdir from a signal context (which is unsafe
    // because mkdir can call malloc and deadlock).
    std::error_code ec;
    std::filesystem::create_directories(dataPath + "/crashdumps", ec);
    cursestone::CrashDump crashDump(dataPath + "/crashdumps");
    g_crashDump = &crashDump;
    // Diagnostics writes its non-fatal error reports here too.
    cursestone::diagnostics::Diagnostics::get().setDumpDir(dataPath);

    // Route uncaught C++ exceptions into the crash/report path.
    std::set_terminate(handleTerminate);

    // First-run setup: NO interactive wizard anymore. The server is
    // fully playable straight out of the box — if there is no config
    // yet, the default config files are written SILENTLY (no stdin
    // prompts, which also broke headless/systemd/Android runs where
    // stdin is closed and fgets returned EOF for every question).
    // Operators customize settings by editing the generated files
    // afterwards. Server::start() also seeds any missing config file.
    {
        wizard::Installer installer(dataPath);
        if (installer.shouldRun()) {
            if (installer.generateDefaultConfig()) {
                utils::MainLogger::get().info(
                    "First run: default config written (no setup questions). "
                    "Edit cursestone.yml / server.properties to customize.");
            } else {
                // Config file could not be created — loadConfig() falls
                // back to built-in defaults, so the server still boots.
                fputs("Could not write default config; using built-in defaults.\n", stderr);
            }
        }
    }

    // Boot the server singleton
    auto& server = Server::getInstance();
    // Point the server at g_stopRequested so runTickLoop() can poll it
    // on the main thread. Must be wired up before runTickLoop() starts
    // spinning; start() itself doesn't touch signals so ordering with
    // it doesn't matter.
    server.setExternalStopFlag(&g_stopRequested);
    server.start(dataPath);
    server.runTickLoop();

    // Flush + shutdown the async logger before exiting. This drains
    // any remaining log entries in the ring buffer and joins the worker
    // thread. Without this, log lines written during shutdown would
    // be lost.
    mcp_main_logger_async_destroy();
    return 0;
}
