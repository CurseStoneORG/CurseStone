/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

 * CurseStone | Open Source Server Software
 *
 * Copyright (C) 2026 CurseStoneORG
 *
 * Developed by CurseStoneORG.
 * Built for an open and extensible Minecraft server ecosystem.
 *
 * Licensed under the GNU Lesser General Public License v3.0 or later.
 * You may study, modify, and redistribute this software under its license.
 *
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 * Repository: https://github.com/CurseStoneORG/CurseStone
 *
 */
/**
 * @brief Wraps the C mcp_main_logger_t as a C++ singleton.
 *
 * Exposes utils::MainLogger::get().info/warn/error/debug(string).
 * All C++ files that log should include this; C files include
 * MainLogger.h.
 */
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

    void info(const std::string& s) {
        auto* lg = &mcp_main_logger_get_instance()->vtable;
        if (lg->info)
            lg->info(lg->impl, s.c_str());
    }
    void warn(const std::string& s) {
        auto* lg = &mcp_main_logger_get_instance()->vtable;
        if (lg->warn)
            lg->warn(lg->impl, s.c_str());
    }
    void error(const std::string& s) {
        auto* lg = &mcp_main_logger_get_instance()->vtable;
        if (lg->error)
            lg->error(lg->impl, s.c_str());
    }
    void debug(const std::string& s) {
        auto* lg = &mcp_main_logger_get_instance()->vtable;
        if (lg->debug)
            lg->debug(lg->impl, s.c_str());
    }
    // FATAL: the console prints the tag in bold red; the log file keeps the
    // long-form "CRITICAL" name via mcp_log_level_name().
    void critical(const std::string& s) {
        auto* lg = &mcp_main_logger_get_instance()->vtable;
        if (lg->critical)
            lg->critical(lg->impl, s.c_str());
    }
    // Game chat mirror (Server::broadcastMessage): pushes MCP_LOG_CHAT so the
    // console tags the line "CHAT" instead of "INFO". Rides the generic
    // log() vtable entry — implementations forward any level they receive.
    void chat(const std::string& s) {
        auto* lg = &mcp_main_logger_get_instance()->vtable;
        if (lg->log)
            lg->log(lg->impl, MCP_LOG_CHAT, s.c_str());
    }
    // Level-explicit dispatch (the Diagnostic Engine renders severity ->
    // level directly; maps to the vtable entry of the same meaning).
    void log(mcp_log_level_t lvl, const std::string& s) {
        auto* lg = &mcp_main_logger_get_instance()->vtable;
        switch (lvl) {
        case MCP_LOG_DEBUG:
            if (lg->debug)
                lg->debug(lg->impl, s.c_str());
            break;
        case MCP_LOG_NOTICE:
        case MCP_LOG_INFO:
            if (lg->info)
                lg->info(lg->impl, s.c_str());
            break;
        case MCP_LOG_WARN:
            if (lg->warn)
                lg->warn(lg->impl, s.c_str());
            break;
        case MCP_LOG_ERROR:
            if (lg->error)
                lg->error(lg->impl, s.c_str());
            break;
        case MCP_LOG_CRITICAL:
        case MCP_LOG_EMERGENCY:
            if (lg->critical)
                lg->critical(lg->impl, s.c_str());
            break;
        case MCP_LOG_CHAT:
            chat(s);
            break;
        default:
            if (lg->critical)
                lg->critical(lg->impl, s.c_str());
            break;
        }
    }
    // Verbatim passthrough (CurseStone Diagnostic Engine blocks): the text is
    // written as-is, no timestamp or level tag, newlines preserved. The block
    // carries its own § color codes, which the console layer converts to ANSI
    // and the log file strips.
    void raw(const std::string& s) {
        auto* lg = &mcp_main_logger_get_instance()->vtable;
        if (lg->raw)
            lg->raw(lg->impl, s.c_str());
    }

  private:
    MainLogger() = default;
};

} // namespace cursestone::utils
