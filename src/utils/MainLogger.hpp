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
