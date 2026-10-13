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
/**
 * @brief Writes a crash dump when the server hits a fatal error.
 *
 * Produces a JSON file in the crash dumps directory plus a
 * human-readable .log companion containing the backtrace, system
 * info, server version, loaded plugins, and recent diagnostics
 * context.
 */
#pragma once
#include <string>
namespace cursestone {
/**
 * @brief Captures fatal-error state to disk.
 */
class CrashDump {
public:
    explicit CrashDump(std::string data_path) : data_path_(std::move(data_path)) {}

    /**
     * @brief Serializes the crash context to the dump files.
     *
     * Invoked from the fatal signal handler. The Diagnostics renderers
     * used here are try_lock based, so they do not deadlock when called
     * from that context.
     *
     * @param reason  short, human-readable description of the failure.
     * @param trace   formatted backtrace to embed in the dump.
     */
    void crash(const std::string& reason, const std::string& trace);
private:
    std::string data_path_;
};
} // namespace
