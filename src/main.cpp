// SPDX-License-Identifier: LGPL-3.0-or-later
// AtomikPE entry point.
// Initializes the server singleton and runs the main tick loop.
#include "main.h"
#include "Server.h"
#include "CrashDump.h"
#include "wizard/Installer.h"
#include "logger/AsyncMainLogger.h"
#include "utils/MainLogger.hpp"

#include <iostream>
#include <csignal>
#include <atomic>
#include <thread>
#include <chrono>
#include <filesystem>
#include <execinfo.h>   // backtrace, backtrace_symbols
#include <unistd.h>     // STDERR_FILENO
#include <cstring>      // strlen

namespace {
    std::atomic<bool> g_shouldRun{true};
    atomikpe::CrashDump* g_crashDump = nullptr;

    void handleSignal(int sig) {
        // SIGINT / SIGTERM → graceful shutdown
        if (sig == SIGINT || sig == SIGTERM) {
            g_shouldRun.store(false, std::memory_order_relaxed);
            atomikpe::Server::getInstance().shutdown();
            return;
        }
        // SIGSEGV / SIGABRT / SIGFPE / SIGILL → crashdump
        const char* reason = "unknown signal";
        switch (sig) {
            case SIGSEGV: reason = "SIGSEGV (segmentation fault)"; break;
            case SIGABRT: reason = "SIGABRT (abort)"; break;
            case SIGFPE:  reason = "SIGFPE (floating-point exception)"; break;
            case SIGILL:  reason = "SIGILL (illegal instruction)"; break;
            case SIGBUS:  reason = "SIGBUS (bus error)"; break;
        }

        // Best-effort backtrace (size 32 max).
        void* frames[32];
        int n = backtrace(frames, 32);
        char** syms = backtrace_symbols(frames, n);

        std::string trace;
        for (int i = 0; i < n; ++i) {
            trace += (syms ? syms[i] : "??");
            trace += '\n';
        }
        free(syms);

        // Also dump to stderr for immediate visibility.
        const char header[] = "\n=== AtomikPE crash ===\n";
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
}

int main(int argc, char* argv[]) {
    using namespace atomikpe;

    // Banner — use fputs instead of std::cout to avoid mixing C and
    // C++ I/O streams (the async logger uses fputs on stdout).
    {
        std::string banner = std::string(ATOMIKPE_NAME) + " v" +
            ATOMIKPE_VERSION + " \"" + ATOMIKPE_CODENAME + "\"\n" +
            "Targeting Minecraft " + MINECRAFT_VERSION +
            " (protocol " + std::to_string(PROTOCOL_VERSION) + ")\n\n";
        fputs(banner.c_str(), stdout);
        fflush(stdout);
    }

    // Install signal handlers for clean shutdown
    std::signal(SIGINT,  handleSignal);
    std::signal(SIGTERM, handleSignal);
    // Install crash handlers — these produce a crashdump JSON file in
    // <dataPath>/crashdumps/ before the process dies, so we can
    // post-mortem-debug freezes and crashes.
    std::signal(SIGSEGV, handleSignal);
    std::signal(SIGABRT, handleSignal);
    std::signal(SIGFPE,  handleSignal);
    std::signal(SIGILL,  handleSignal);
    std::signal(SIGBUS,  handleSignal);

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
    atomikpe::CrashDump crashDump(dataPath + "/crashdumps");
    g_crashDump = &crashDump;

    // First-run setup wizard
    wizard::Installer installer(dataPath);
    if (installer.shouldRun()) {
        if (!installer.runWizard()) {
            fputs("Setup failed; generating default config instead.\n", stderr);
            installer.generateDefaultConfig();
        }
    }

    // Boot the server singleton
    auto& server = Server::getInstance();
    server.start(dataPath);
    server.runTickLoop();

    // Flush + shutdown the async logger before exiting. This drains
    // any remaining log entries in the ring buffer and joins the worker
    // thread. Without this, log lines written during shutdown would
    // be lost.
    mcp_main_logger_async_destroy();
    return 0;
}
