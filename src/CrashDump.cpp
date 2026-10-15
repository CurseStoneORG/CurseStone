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
#include "CrashDump.h"
#include "diagnostics/Diagnostics.h"
#include "utils/MainLogger.hpp"
#include "CurseStone.h"
#include <fstream>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <cstdlib>
namespace cursestone {
namespace {

std::string utcStamp(uint64_t wall_ms, const char* fmt) {
    std::time_t t = static_cast<std::time_t>(wall_ms / 1000);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[48];
    std::strftime(buf, sizeof(buf), fmt, &tm);
    return buf;
}

} // namespace

void CrashDump::crash(const std::string& reason, const std::string& trace) {
    auto t = std::chrono::system_clock::now();
    auto t_c = std::chrono::system_clock::to_time_t(t);
    uint64_t now_ms = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(t.time_since_epoch()).count());

    using cursestone::diagnostics::Diagnostics;
    // Pull the always-on context. These renderers use try_lock, so they are
    // safe to call from the signal handler that invokes crash().
    std::string context = Diagnostics::get().renderContext();
    std::string players = Diagnostics::get().renderPlayers();
    std::string errors = Diagnostics::get().renderRecentErrors(20);
    std::string packets = Diagnostics::get().renderRecentPackets(40);

    std::ostringstream fname;
    fname << data_path_ << "/crashdump_" << std::put_time(std::gmtime(&t_c), "%Y%m%d_%H%M%S")
          << ".json";
    std::string json_path = fname.str();
    std::string log_path = json_path + ".log";

    auto esc = diagnostics::json_escape;

    std::ofstream f(json_path);
    if (f) {
        f << "{\n"
          << "  \"version\": \"" << CURSESTONE_API << "\",\n"
          << "  \"codename\": \"" << CURSESTONE_CODENAME << "\",\n"
          << "  \"minecraft\": \"" << MINECRAFT_VERSION << "\",\n"
          << "  \"protocol\": " << PROTOCOL_VERSION << ",\n"
          << "  \"timestamp_utc\": \"" << utcStamp(now_ms, "%Y-%m-%dT%H:%M:%SZ") << "\",\n"
          << "  \"reason\": \"" << esc(reason) << "\",\n"
          << "  \"trace\": \"" << esc(trace) << "\",\n"
          << "  \"context\": \"" << esc(context) << "\",\n"
          << "  \"players\": \"" << esc(players) << "\",\n"
          << "  \"recent_errors\": \"" << esc(errors) << "\",\n"
          << "  \"recent_packets\": \"" << esc(packets) << "\"\n"
          << "}\n";
        f.flush();
    }

    // Human readable companion.
    std::ofstream lf(log_path);
    if (lf) {
        lf << "CurseStone crash dump\n"
           << "====================\n"
           << "Version:    " << CURSESTONE_API << " \"" << CURSESTONE_CODENAME << "\"\n"
           << "Minecraft:  " << MINECRAFT_VERSION << " (protocol " << PROTOCOL_VERSION << ")\n"
           << "Time (UTC): " << utcStamp(now_ms, "%Y-%m-%d %H:%M:%S") << "\n"
           << "Reason:     " << reason << "\n"
           << "\n--- Context ---\n"
           << context << "\n--- Players ---\n"
           << players << "\n--- Recent errors ---\n"
           << errors << "\n--- Recent packets ---\n"
           << packets << "\n--- Backtrace ---\n"
           << trace << "\n";
        lf.flush();
    }

    utils::MainLogger::get().error("Server crash: " + reason);
    utils::MainLogger::get().error("Crashdump written to " + json_path);
}
} // namespace
