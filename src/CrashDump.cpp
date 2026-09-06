#include "CrashDump.h"
#include "utils/MainLogger.hpp"
#include "main.h"
#include <fstream>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <cstdlib>
namespace atomikpe {
namespace {
// Escape a string for inclusion as a JSON string literal. Backslashes,
// quotes and control chars are escaped per RFC 8259. Without this,
// backtrace_symbols output (which contains slashes, parens and plus
// signs) would silently truncate or corrupt the JSON file.
std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
}
} // namespace

void CrashDump::crash(const std::string& reason, const std::string& trace) {
    auto t = std::chrono::system_clock::now();
    auto t_c = std::chrono::system_clock::to_time_t(t);
    std::ostringstream fname;
    fname << data_path_ << "/crashdump_"
          << std::put_time(std::gmtime(&t_c), "%Y%m%d_%H%M%S") << ".json";

    // Also write a human-readable .log companion next to the JSON,
    // because raw JSON is hard to read in a panic.
    std::string log_path = fname.str() + ".log";

    std::ofstream f(fname.str());
    if (f) {
        f << "{\n"
          << "  \"version\": \"" << ATOMIKPE_VERSION << "\",\n"
          << "  \"codename\": \"" << ATOMIKPE_CODENAME << "\",\n"
          << "  \"minecraft\": \"" << MINECRAFT_VERSION << "\",\n"
          << "  \"protocol\": " << PROTOCOL_VERSION << ",\n"
          << "  \"timestamp_utc\": \""
          << std::put_time(std::gmtime(&t_c), "%Y-%m-%dT%H:%M:%SZ") << "\",\n"
          << "  \"reason\": \"" << json_escape(reason) << "\",\n"
          << "  \"trace\": \"" << json_escape(trace) << "\"\n"
          << "}\n";
        f.flush();
    }

    // Best-effort log file (human readable).
    std::ofstream lf(log_path);
    if (lf) {
        lf << "AtomikPE crash dump\n"
           << "==================\n"
           << "Version:    " << ATOMIKPE_VERSION << " \"" << ATOMIKPE_CODENAME << "\"\n"
           << "Minecraft:  " << MINECRAFT_VERSION << " (protocol " << PROTOCOL_VERSION << ")\n"
           << "Time (UTC): " << std::put_time(std::gmtime(&t_c), "%Y-%m-%d %H:%M:%S") << "\n"
           << "Reason:     " << reason << "\n"
           << "\n--- Backtrace ---\n" << trace << "\n";
        lf.flush();
    }

    utils::MainLogger::get().error("Server crash: " + reason);
    utils::MainLogger::get().error("Crashdump written to " + fname.str());
}
} // namespace
