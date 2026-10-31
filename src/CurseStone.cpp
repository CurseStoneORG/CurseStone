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
#include <cstring> // strlen
#include <string>
#include <cstdio>    // snprintf
#include <exception> // std::set_terminate / current_exception
#include <cstdint>
#include <vector>
#if defined(__linux__) || defined(__APPLE__)
#include <sys/resource.h> // getrlimit/setrlimit (RLIMIT_CORE)
#include <sys/types.h>
#include <sys/wait.h>     // waitpid (crash frame symbolizer)
#include <unistd.h>       // read/pread/write/dup2/execvp/fork
#include <fcntl.h>        // open/O_CLOEXEC
#include <dlfcn.h>        // dladdr (fallback symbol names)
#endif
#if defined(__APPLE__)
#include <mach-o/dyld.h>  // _NSGetExecutablePath
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
#if defined(CURSESTONE_FORCE_UNWIND)
// Testing knob (-DCURSESTONE_FORCE_UNWIND): force the _Unwind_Backtrace
// collection path on any host so the exact backend used on Android/Termux
// (bionic) can be exercised from a glibc build.
#define CURSESTONE_HAVE_UNWIND 1
#include <unwind.h>
#elif defined(__GLIBC__) || defined(__linux__) && !defined(__ANDROID__)
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
#include <io.h>
#ifndef STDERR_FILENO
#define STDERR_FILENO 2
#endif
// The CRT exposes write() as _write().
#define write _write
#else
// write() lives in <unistd.h> on POSIX platforms.
#include <unistd.h>
#include <sys/wait.h> // waitpid for the forked crash-dump child
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

// Set while the crash reporting path (backtrace + symbolizer + crashdump)
// is running. A second fatal signal arriving during reporting means the
// stack or the heap is too corrupted to continue safely: we bail out to the
// default disposition instead of recursing forever.
volatile std::sig_atomic_t g_inCrashHandler = 0;

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
// libunwind-based frame collection (Android / bionic, or the testing knob).
struct UnwindContext {
    void** frames;
    int max_frames;
    int count;
};

static _Unwind_Reason_Code unwind_cb(struct _Unwind_Context* ctx, void* arg) {
    auto* uc = static_cast<UnwindContext*>(arg);
    if (uc->count >= uc->max_frames)
        return _URC_END_OF_STACK;
    _Unwind_Ptr ip = _Unwind_GetIP(ctx);
    if (ip == 0)
        return _URC_NO_REASON;
    uc->frames[uc->count++] = reinterpret_cast<void*>(static_cast<uintptr_t>(ip));
    return _URC_NO_REASON;
}
#endif

// Shared by every backtrace backend: upper bound of collected frames.
constexpr int kCrashMaxFrames = 32;

// ---- Crash frame symbolization ------------------------------------------
//
// Raw "#N 0x..." addresses are useless without symbols. The binary is built
// with -g in every build type (CMakeLists.txt / build_local.sh), so the
// frames can be resolved to function names and source lines with an
// external symbolizer. We try, in order:
//   1. llvm-symbolizer (ships with clang; available in Termux: pkg install clang)
//   2. addr2line       (binutils; preinstalled on desktop Linux distros)
// If neither tool exists we fall back to dladdr() names — the binaries link
// with -rdynamic / ENABLE_EXPORTS, so global functions still resolve — and
// if that fails too, the raw addresses are printed exactly as before.
//
// Runtime address -> file offset: for PIE executables (the default
// everywhere today, Termux included) file_offset = runtime - load_bias,
// with load_bias = lowest mapping start of the executable minus its lowest
// PT_LOAD p_vaddr (0 for PIE). Parsing the maps + the ELF program headers
// ourselves keeps this lock-free (no dl_iterate_phdr) and correct for both
// PIE and non-PIE.
//
// Everything here runs inside the signal handler. Between fork() and
// exec*() only async-signal-safe syscalls are used (pipe/dup2/write/read/
// close/waitpid/snprintf on stack buffers); the external symbolizer runs
// in a forked child, matching the fork-based crashdump below.

#if (defined(CURSESTONE_HAVE_EXECINFO) || defined(CURSESTONE_HAVE_UNWIND)) && \
    (defined(__linux__) || defined(__APPLE__))

static bool getCrashExePath(char* buf, size_t cap) {
#if defined(__linux__)
    ssize_t n = ::readlink("/proc/self/exe", buf, cap - 1);
    if (n <= 0)
        return false;
    buf[n] = '\0';
    return true;
#elif defined(__APPLE__)
    uint32_t size = static_cast<uint32_t>(cap);
    return ::_NSGetExecutablePath(buf, &size) == 0;
#else
    (void)buf;
    (void)cap;
    return false;
#endif
}

#if defined(__linux__)
static int hexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Lowest start / highest end of every mapping that backs `exe`.
static bool getExeMapRange(const char* exe, uintptr_t& lo, uintptr_t& hi) {
    int fd = ::open("/proc/self/maps", O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return false;
    const size_t exeLen = std::strlen(exe);
    if (exeLen == 0) {
        ::close(fd);
        return false;
    }
    char chunk[4096];
    char line[1024];
    size_t lineLen = 0;
    bool have = false;
    lo = ~static_cast<uintptr_t>(0);
    hi = 0;
    ssize_t got;
    while ((got = ::read(fd, chunk, sizeof(chunk))) > 0) {
        for (ssize_t i = 0; i < got; ++i) {
            char c = chunk[i];
            if (c != '\n') {
                if (lineLen + 1 < sizeof(line))
                    line[lineLen++] = c;
                continue;
            }
            line[lineLen] = '\0';
            // Match only lines whose mapping path *is* the executable:
            // "start-end perms offset dev inode <exe>".
            if (lineLen > exeLen && line[lineLen - exeLen - 1] == ' ' &&
                std::strcmp(line + lineLen - exeLen, exe) == 0) {
                uintptr_t s = 0, e = 0;
                const char* p = line;
                int v;
                for (; *p && *p != '-'; ++p) {
                    v = hexVal(*p);
                    if (v < 0) break;
                    s = s * 16 + static_cast<uintptr_t>(v);
                }
                if (*p == '-') {
                    for (++p; *p && *p != ' '; ++p) {
                        v = hexVal(*p);
                        if (v < 0) break;
                        e = e * 16 + static_cast<uintptr_t>(v);
                    }
                    if (s < lo) lo = s;
                    if (e > hi) hi = e;
                    have = true;
                }
            }
            lineLen = 0;
        }
    }
    ::close(fd);
    return have && hi > lo;
}
#endif // __linux__

// Lowest PT_LOAD p_vaddr of the executable (0 for PIE). Supports ELF32 and
// ELF64; returns 0 when the header cannot be parsed.
static uintptr_t getExeMinLoadVaddr(const char* exe) {
    int fd = ::open(exe, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        fd = ::open("/proc/self/exe", O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return 0;
    unsigned char eh[64];
    uintptr_t minV = 0;
    bool have = false;
    if (::pread(fd, eh, sizeof(eh), 0) == sizeof(eh) && eh[0] == 0x7F && eh[1] == 'E' &&
        eh[2] == 'L' && eh[3] == 'F') {
        if (eh[4] == 2) { // ELF64
            uint64_t phoff = 0;
            uint16_t phentsize = 0, phnum = 0;
            std::memcpy(&phoff, eh + 0x20, 8);
            std::memcpy(&phentsize, eh + 0x36, 2);
            std::memcpy(&phnum, eh + 0x38, 2);
            if (phnum > 128) phnum = 128;
            for (uint16_t i = 0; i < phnum; ++i) {
                unsigned char ph[56];
                if (::pread(fd, ph, sizeof(ph),
                            static_cast<off_t>(phoff) + static_cast<off_t>(i) * phentsize) !=
                    sizeof(ph))
                    break;
                uint32_t ptype = 0;
                std::memcpy(&ptype, ph, 4);
                if (ptype != 1) continue; // PT_LOAD
                uint64_t vaddr = 0;
                std::memcpy(&vaddr, ph + 0x10, 8);
                if (!have || vaddr < minV) {
                    minV = vaddr;
                    have = true;
                }
            }
        } else if (eh[4] == 1) { // ELF32
            uint32_t phoff = 0;
            uint16_t phentsize = 0, phnum = 0;
            std::memcpy(&phoff, eh + 0x1C, 4);
            std::memcpy(&phentsize, eh + 0x2A, 2);
            std::memcpy(&phnum, eh + 0x2C, 2);
            if (phnum > 128) phnum = 128;
            for (uint16_t i = 0; i < phnum; ++i) {
                unsigned char ph[32];
                if (::pread(fd, ph, sizeof(ph),
                            static_cast<off_t>(phoff) + static_cast<off_t>(i) * phentsize) !=
                    sizeof(ph))
                    break;
                uint32_t ptype = 0;
                std::memcpy(&ptype, ph, 4);
                if (ptype != 1) continue; // PT_LOAD
                uint32_t vaddr = 0;
                std::memcpy(&vaddr, ph + 0x8, 4);
                if (!have || vaddr < minV) {
                    minV = vaddr;
                    have = true;
                }
            }
        }
    }
    ::close(fd);
    return have ? minV : 0;
}

// Runs `argv` in a forked child and captures its stdout. When stdinData is
// non-null it is written to the child's stdin first (payloads here are a
// few hundred bytes, well under the pipe buffer, so no deadlock is
// possible before the parent starts draining stdout).
static std::string runCrashTool(char* const argv[], const char* stdinData, size_t stdinLen) {
    int outPipe[2];
    if (::pipe(outPipe) != 0)
        return "";
    int inPipe[2] = {-1, -1};
    if (stdinData != nullptr && ::pipe(inPipe) != 0) {
        ::close(outPipe[0]);
        ::close(outPipe[1]);
        return "";
    }
    pid_t pid = ::fork();
    if (pid < 0) {
        ::close(outPipe[0]);
        ::close(outPipe[1]);
        if (stdinData != nullptr) {
            ::close(inPipe[0]);
            ::close(inPipe[1]);
        }
        return "";
    }
    if (pid == 0) {
        ::dup2(outPipe[1], STDOUT_FILENO);
        if (stdinData != nullptr)
            ::dup2(inPipe[0], STDIN_FILENO);
        ::close(outPipe[0]);
        ::close(outPipe[1]);
        if (stdinData != nullptr) {
            ::close(inPipe[0]);
            ::close(inPipe[1]);
        }
        ::execvp(argv[0], argv); // only returns on failure
        ::_exit(127);
    }
    ::close(outPipe[1]);
    if (stdinData != nullptr) {
        ::close(inPipe[0]);
        size_t off = 0;
        while (off < stdinLen) {
            ssize_t w = ::write(inPipe[1], stdinData + off, stdinLen - off);
            if (w <= 0) break;
            off += static_cast<size_t>(w);
        }
        ::close(inPipe[1]);
    }
    std::string out;
    out.reserve(2048);
    char buf[2048];
    ssize_t r;
    while ((r = ::read(outPipe[0], buf, sizeof(buf))) > 0) {
        if (out.size() < 64 * 1024)
            out.append(buf, static_cast<size_t>(r));
    }
    ::close(outPipe[0]);
    int st = 0;
    ::waitpid(pid, &st, 0);
    return out;
}

struct CrashFrameInfo {
    bool haveFunc = false;
    bool haveLoc = false;
    std::string func;
    std::string loc;
    std::string inlined; // pre-rendered "(inlined) ..." lines
};

// Parses symbolizer output into one entry per query. With -a both tools
// print a "0x..." line before each record; each record then holds one or
// more (function, location) line pairs — extra pairs are inlined frames.
// llvm-symbolizer separates records with blank lines, addr2line does not.
static void parseSymbolizerOutput(const std::string& s, int queries,
                                  std::vector<CrashFrameInfo>& rec) {
    int record = -1;
    size_t pos = 0;
    std::string pending; // function line waiting for its location line
    while (pos < s.size()) {
        size_t eol = s.find('\n', pos);
        if (eol == std::string::npos) eol = s.size();
        size_t len = eol - pos;
        const char* p = s.data() + pos;
        pos = eol + 1;
        if (len >= 2 && p[0] == '0' && p[1] == 'x') {
            ++record;
            if (record >= queries) break;
            pending.clear();
            continue;
        }
        if (record < 0 || len == 0) {
            pending.clear();
            continue;
        }
        if (p[len - 1] == '\r') --len;
        if (len == 0) {
            pending.clear();
            continue;
        }
        std::string tok(p, len);
        if (pending.empty()) {
            pending = tok;
            continue;
        }
        std::string fn;
        fn.swap(pending);
        if (rec[record].func.empty() && !rec[record].haveFunc) {
            rec[record].func = fn;
            rec[record].haveFunc = (fn != "??");
            rec[record].loc = tok;
            rec[record].haveLoc = (tok.rfind("??", 0) != 0);
        } else {
            rec[record].inlined += "\n      (inlined) ";
            rec[record].inlined += fn;
            rec[record].inlined += "  ";
            rec[record].inlined += tok;
        }
    }
}

// Appends one enriched "#N 0x...  <func>  <file:line>" line per frame.
// Returns true when the annotated block was produced (exe path resolved);
// the caller then skips the plain raw-address block.
static bool symbolizeCrashFrames(std::string& out, void* const* frames, int n) {
    char exe[1024];
    if (!getCrashExePath(exe, sizeof(exe)))
        return false;
    uintptr_t lo = 0, hi = 0;
    bool haveRange = false;
#if defined(__linux__)
    haveRange = getExeMapRange(exe, lo, hi);
#endif
    uintptr_t bias = 0;
    if (haveRange)
        bias = lo - getExeMinLoadVaddr(exe);
    else {
        Dl_info di{};
        if (::dladdr(reinterpret_cast<void*>(&symbolizeCrashFrames), &di) && di.dli_fbase)
            bias = reinterpret_cast<uintptr_t>(di.dli_fbase); // PIE assumption
    }

    uintptr_t offs[kCrashMaxFrames];
    int offFrame[kCrashMaxFrames];
    bool inside[kCrashMaxFrames] = {};
    int m = 0;
    for (int i = 0; i < n; ++i) {
        uintptr_t a = reinterpret_cast<uintptr_t>(frames[i]);
        inside[i] = haveRange ? (a >= lo && a < hi) : (bias != 0 && a >= bias);
        if (inside[i] && bias != 0 && a >= bias) {
            offs[m] = a - bias;
            offFrame[m] = i;
            ++m;
        }
    }

    std::vector<CrashFrameInfo> info(n);
    bool haveSymbols = false;
    if (m > 0) {
        std::vector<CrashFrameInfo> symInfo(m);
        // 1) llvm-symbolizer: addresses on stdin.
        char qbuf[kCrashMaxFrames * 24];
        size_t qlen = 0;
        for (int k = 0; k < m; ++k) {
            int w = std::snprintf(qbuf + qlen, sizeof(qbuf) - qlen, "0x%lx\n",
                                  static_cast<unsigned long>(offs[k]));
            if (w <= 0) break;
            qlen += static_cast<size_t>(w);
        }
        char objArg[16 + sizeof(exe)];
        std::snprintf(objArg, sizeof(objArg), "--obj=%s", exe);
        char a0[] = "llvm-symbolizer";
        char* llvmArgv[] = {a0, objArg, const_cast<char*>("-f"), const_cast<char*>("-C"),
                            const_cast<char*>("-i"), const_cast<char*>("-a"), nullptr};
        std::string sym = runCrashTool(llvmArgv, qbuf, qlen);
        if (!sym.empty()) {
            parseSymbolizerOutput(sym, m, symInfo);
            for (int k = 0; k < m; ++k)
                if (symInfo[k].haveFunc || symInfo[k].haveLoc) {
                    haveSymbols = true;
                    break;
                }
        }
        // 2) addr2line: addresses as argv.
        if (!haveSymbols) {
            for (int k = 0; k < m; ++k) symInfo[k] = CrashFrameInfo{};
            char a0[] = "addr2line";
            char* addr2Argv[8 + kCrashMaxFrames];
            int c = 0;
            addr2Argv[c++] = a0;
            addr2Argv[c++] = const_cast<char*>("-e");
            addr2Argv[c++] = exe;
            addr2Argv[c++] = const_cast<char*>("-f");
            addr2Argv[c++] = const_cast<char*>("-C");
            addr2Argv[c++] = const_cast<char*>("-i");
            addr2Argv[c++] = const_cast<char*>("-a");
            char hexArgs[kCrashMaxFrames][24];
            for (int k = 0; k < m; ++k) {
                std::snprintf(hexArgs[k], sizeof(hexArgs[k]), "0x%lx",
                              static_cast<unsigned long>(offs[k]));
                addr2Argv[c++] = hexArgs[k];
            }
            addr2Argv[c] = nullptr;
            std::string sym2 = runCrashTool(addr2Argv, nullptr, 0);
            if (!sym2.empty()) {
                parseSymbolizerOutput(sym2, m, symInfo);
                for (int k = 0; k < m; ++k)
                    if (symInfo[k].haveFunc || symInfo[k].haveLoc) {
                        haveSymbols = true;
                        break;
                    }
            }
        }
        for (int k = 0; k < m; ++k) info[offFrame[k]] = std::move(symInfo[k]);
    }

    for (int i = 0; i < n; ++i) {
        char raw[48];
        std::snprintf(raw, sizeof(raw), "#%d  0x%lx", i,
                      static_cast<unsigned long>(reinterpret_cast<uintptr_t>(frames[i])));
        std::string line(raw);
        if (haveSymbols && info[i].haveFunc) {
            line += "  ";
            line += info[i].func;
            if (info[i].haveLoc) {
                line += "  ";
                line += info[i].loc;
            }
            line += info[i].inlined;
        } else if (inside[i]) {
            bool named = false;
            Dl_info di{};
            if (::dladdr(frames[i], &di) && di.dli_sname) {
                char offb[32];
                std::snprintf(offb, sizeof(offb), "+0x%lx",
                              static_cast<unsigned long>(reinterpret_cast<uintptr_t>(frames[i]) -
                                                         reinterpret_cast<uintptr_t>(di.dli_saddr)));
                line += "  ";
                line += di.dli_sname;
                line += offb;
                named = true;
            } else if (bias != 0 && reinterpret_cast<uintptr_t>(frames[i]) >= bias) {
                char offb[48];
                std::snprintf(offb, sizeof(offb), "  CurseStone+0x%lx",
                              reinterpret_cast<uintptr_t>(frames[i]) - bias);
                line += offb;
                named = true;
            }
            if (!named)
                line += "  (inside CurseStone)";
        } else {
            Dl_info di{};
            if (::dladdr(frames[i], &di) && di.dli_fname) {
                const char* nm = std::strrchr(di.dli_fname, '/');
                nm = nm ? nm + 1 : di.dli_fname;
                line += "  (external: ";
                line += nm;
                line += ")";
            } else {
                line += "  (external library)";
            }
        }
        out += line;
        out += '\n';
    }
    if (!haveSymbols) {
        for (int i = 0; i < n; ++i) {
            if (inside[i]) {
                out += "(no symbolizer found on PATH — install binutils (addr2line) or clang "
                       "(llvm-symbolizer) to see function names and source lines)\n";
                break;
            }
        }
    }
    return true;
}
#endif // frame symbolization support

std::string capture_backtrace(int max_frames = 32) {
    std::string out;
    out.reserve(1024);

    void* frames[kCrashMaxFrames];
    int n = 0;

#if defined(CURSESTONE_HAVE_EXECINFO)
    n = backtrace(frames, max_frames < kCrashMaxFrames ? max_frames : kCrashMaxFrames);
    if (n <= 0) {
        out = "(no frames captured — backtrace() returned nothing)\n";
        n = 0;
    }
#elif defined(CURSESTONE_HAVE_UNWIND)
    UnwindContext uc{frames, max_frames < kCrashMaxFrames ? max_frames : kCrashMaxFrames, 0};
    _Unwind_Backtrace(unwind_cb, &uc);
    n = uc.count;
    if (n == 0) {
        out = "(no frames captured — _Unwind_Backtrace returned immediately)\n";
    }
#else
    (void)frames;
    (void)max_frames;
    out = "(stack trace unavailable on this platform)\n";
#endif

    // Plain raw addresses, the historical format. Kept as the fallback when
    // the symbolizer cannot even resolve the executable path.
    std::string raw;
    raw.reserve(512);
    for (int i = 0; i < n; ++i) {
        char line[64];
        int len = std::snprintf(line, sizeof(line), "#%d  0x%lx\n", i,
                                static_cast<unsigned long>(reinterpret_cast<uintptr_t>(frames[i])));
        if (len > 0)
            raw.append(line, static_cast<size_t>(len));
    }

#if (defined(CURSESTONE_HAVE_EXECINFO) || defined(CURSESTONE_HAVE_UNWIND)) && \
    (defined(__linux__) || defined(__APPLE__))
    // Preferred output: every frame annotated with its function name and
    // source location (frames in system libraries are labeled as external).
    std::string annotated;
    if (n > 0 && symbolizeCrashFrames(annotated, frames, n)) {
        out += annotated;
        return out;
    }
#endif

    out += raw;
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
        cursestone::diagnostics::Diagnostics::get().writeReport("terminate", msg,
                                                                "std::terminate called");
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
    // Fatal signal while already reporting a crash: stop unwinding/reporting
    // and die with the default disposition (prevents infinite recursion).
    if (g_inCrashHandler) {
        std::signal(sig, SIG_DFL);
        std::raise(sig);
        return;
    }
    g_inCrashHandler = 1;
    // SIGSEGV / SIGABRT / SIGFPE / SIGILL / SIGBUS → crashdump
    const char* reason = "unknown signal";
    switch (sig) {
    case SIGSEGV:
        reason = "SIGSEGV (segmentation fault)";
        break;
    case SIGABRT:
        reason = "SIGABRT (abort)";
        break;
    case SIGFPE:
        reason = "SIGFPE (floating-point exception)";
        break;
    case SIGILL:
        reason = "SIGILL (illegal instruction)";
        break;
#ifdef SIGBUS
    case SIGBUS:
        reason = "SIGBUS (bus error)";
        break;
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
#if !defined(_WIN32)
        // Write the dump from a forked child: the crashing process may hold
        // malloc arena locks or the logger mutex, so doing the malloc-heavy
        // dump work in-process can deadlock before a single byte is written.
        // fork() and waitpid() are async-signal-safe.
        pid_t child = fork();
        if (child == 0) {
            g_crashDump->crash(reason, trace);
            _exit(0);
        }
        if (child > 0) {
            int st = 0;
            waitpid(child, &st, 0);
        }
#else
        g_crashDump->crash(reason, trace);
#endif
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
        std::string banner = std::string(CURSESTONE_NAME) + " v" + CURSESTONE_API + " \"" +
                             CURSESTONE_CODENAME + "\"\n" + "Targeting Minecraft " +
                             MINECRAFT_VERSION + " (protocol " + std::to_string(PROTOCOL_VERSION) +
                             ")\n\n";
        fputs(banner.c_str(), stdout);
        fflush(stdout);
    }

    // Install signal handlers for clean shutdown
    std::signal(SIGINT, handleSignal);
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
    std::signal(SIGFPE, handleSignal);
    std::signal(SIGILL, handleSignal);
#ifdef SIGBUS
    std::signal(SIGBUS, handleSignal);
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
