// CrashDump — captured on a fatal error. Writes a JSON-formatted
// dump to crashdumps/<timestamp>.json containing backtrace, system
// info, server version, loaded plugins, etc.
#pragma once
#include <string>
namespace mcppeserver {
class CrashDump {
public:
    explicit CrashDump(std::string data_path) : data_path_(std::move(data_path)) {}
    void crash(const std::string& reason, const std::string& trace);
private:
    std::string data_path_;
};
} // namespace
