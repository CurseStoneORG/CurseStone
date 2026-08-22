#include "CrashDump.h"
#include "utils/MainLogger.hpp"
#include <fstream>
#include <chrono>
#include <iomanip>
#include <sstream>
namespace mcppeserver {
void CrashDump::crash(const std::string& reason, const std::string& trace) {
    auto t = std::chrono::system_clock::now();
    auto t_c = std::chrono::system_clock::to_time_t(t);
    std::ostringstream fname;
    fname << data_path_ << "/crashdump_" << std::put_time(std::gmtime(&t_c), "%Y%m%d_%H%M%S") << ".json";
    std::ofstream f(fname.str());
    f << "{\n  \"reason\": \"" << reason << "\",\n  \"trace\": \"" << trace << "\"\n}\n";
    utils::MainLogger::get().error("Server crash: " + reason);
    utils::MainLogger::get().error("Crashdump written to " + fname.str());
}
} // namespace
