// SPDX-License-Identifier: LGPL-3.0-or-later
// Diagnostics.h — always-on diagnostic recorder for crashdumps and errors.
//
// Why this exists (feedback from the user):
//   "nuestro crashdump system es muy cerrado y no muestra cosas reveladoras
//    ... mejoralo y que los crashdumps o errores muestren como el de Nukkit".
//
// The old CrashDump only wrote {version, reason, backtrace}. That is useless
// to debug a crash that happens while a player is opening a chest or while a
// malformed packet is being handled: you cannot tell WHICH player, WHAT
// packet, or what the server was doing one tick before it died.
//
// Diagnostics is a tiny, lock-protected singleton that ALWAYS records (no
// /workbench start needed):
//   * the last N packets IN/OUT (who, pid, size)
//   * the last N reported errors (unknown packet, decode failure, exception)
//   * a periodic snapshot of every online player (name, eid, ip, position,
//     chunk, gamemode, open container, time since last activity)
//   * the current "action" string (e.g. "handling pid=0x2c from Steve")
//   * server uptime + current tick
//
// CrashDump and the non-fatal error reports both render this context, so a
// crashdump now reads like a Nukkit crash report instead of a lone backtrace.
//
// Signal-safety: the render helpers take the mutex with try_lock and never
// block, so they are safe to call from main.cpp's SIGSEGV handler. If the
// lock is held (rare — a crash landing mid-record), they emit a marker
// instead of deadlocking.
#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace cursestone {
namespace diagnostics {

// Maximum number of recorded entries in each ring buffer.
constexpr size_t kMaxPacketRecords = 256;
constexpr size_t kMaxErrorRecords  = 128;
constexpr size_t kMaxPlayerSnapshot = 64;

// One recorded packet (IN = client->server, OUT = server->client).
struct PacketRecord {
    uint64_t    wall_ms = 0;   // epoch milliseconds
    bool        out     = false;
    std::string who;           // player name (OUT) or "ip:port" (IN)
    uint8_t     pid     = 0;
    size_t      size    = 0;
};

// One recorded error/diagnostic event.
struct ErrorRecord {
    uint64_t    wall_ms = 0;
    std::string category;   // "packet", "player", "exception", "network", ...
    std::string message;    // short one-line summary (shown on kick)
    std::string detail;     // optional long detail
};

// A cheap snapshot of one online player, refreshed from the main thread.
struct PlayerSnapshot {
    std::string name;
    std::string ip;
    uint16_t    port = 0;
    int64_t     eid = 0;
    double      x = 0.0, y = 0.0, z = 0.0;
    int32_t     cx = 0, cz = 0;
    int         gamemode = 0;
    bool        connected = false;
    bool        logged_in = false;
    bool        spawned = false;
    bool        open_container = false;
    uint64_t    ms_since_activity = 0;
};

// Escape a string for inclusion in a JSON string literal (RFC 8259).
// Shared with CrashDump.cpp so both produce identical, valid JSON.
std::string json_escape(const std::string& s);

class Diagnostics {
public:
    static Diagnostics& get();

    // ---- Configuration (called once at startup) ----
    void setDumpDir(const std::string& dir);   // where error reports are written
    void setUptimeStart(uint64_t wall_ms);     // epoch ms when the server started

    // ---- Hot-path recording (always on, cheap) ----
    void recordPacket(bool out, const std::string& who, uint8_t pid, size_t size);
    // Record an event. If `console` is false it only goes to the ring
    // buffer (used for benign/high-frequency events such as unhandled
    // packet IDs, which would otherwise flood the console).
    void report(const std::string& category, const std::string& message,
                const std::string& detail = {}, bool console = true);
    void setAction(const std::string& action);
    void setTick(uint64_t tick) { tick_.store(tick, std::memory_order_relaxed); }

    // Replace the online-player snapshot (called from the tick loop).
    void updatePlayers(std::vector<PlayerSnapshot> snapshot);

    // Report an error AND write a self-contained report file under
    // <dumpDir>/crashdumps/. Used for non-fatal but serious events
    // (unhandled packet exception, forced kicks). Returns the file path
    // (empty if the file could not be written).
    std::string writeReport(const std::string& category,
                            const std::string& message,
                            const std::string& detail = {});

    // ---- Rendering (safe to call from a signal handler) ----
    std::string renderContext() const;                       // multi-line header
    std::string renderPlayers() const;
    std::string renderRecentErrors(size_t max = 20) const;
    std::string renderRecentPackets(size_t max = 40) const;
    std::string lastErrorSummary() const;                    // one-liner

    // Returns true if the internal lock is currently held; used to warn
    // that a rendered dump may be incomplete.
    bool busy() const;

private:
    Diagnostics() = default;

    static std::string wallToUtc(uint64_t wall_ms);
    static std::string shortTime(uint64_t wall_ms);

    // Rendering helpers that assume mtx_ is already held by the caller.
    std::string renderContextLocked() const;
    std::string renderPlayersLocked() const;
    std::string renderRecentErrorsLocked(size_t max) const;
    std::string renderRecentPacketsLocked(size_t max) const;

    mutable std::mutex mtx_;

    std::string dump_dir_ = ".";
    uint64_t    start_ms_ = 0;
    std::atomic<uint64_t> tick_{0};

    std::string action_ = "(idle)";
    std::vector<PacketRecord> packets_;
    std::vector<ErrorRecord>  errors_;
    std::vector<PlayerSnapshot> players_;

    uint64_t report_seq_ = 0;   // makes report filenames unique
};

} // namespace diagnostics
} // namespace cursestone
