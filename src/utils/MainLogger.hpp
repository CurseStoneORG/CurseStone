// MainLogger.hpp — C++ singleton wrapper over the C mcp_main_logger_t.
// Exposes utils::MainLogger::get().info/warn/error/debug(string).
// All C++ files that log should include this; C files include MainLogger.h.
#pragma once
#include "MainLogger.h"
#include <string>

namespace cursestone::utils {

class MainLogger {
public:
    static MainLogger& get() {
        static MainLogger inst;
        return inst;
    }

    void info (const std::string& s) {
        auto* lg = &mcp_main_logger_get_instance()->vtable;
        if (lg->info)  lg->info(lg->impl, s.c_str());
    }
    void warn (const std::string& s) {
        auto* lg = &mcp_main_logger_get_instance()->vtable;
        if (lg->warn)  lg->warn(lg->impl, s.c_str());
    }
    void error(const std::string& s) {
        auto* lg = &mcp_main_logger_get_instance()->vtable;
        if (lg->error) lg->error(lg->impl, s.c_str());
    }
    void debug(const std::string& s) {
        auto* lg = &mcp_main_logger_get_instance()->vtable;
        if (lg->debug) lg->debug(lg->impl, s.c_str());
    }

private:
    MainLogger() = default;
};

} // namespace cursestone::utils
