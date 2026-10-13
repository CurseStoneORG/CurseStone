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
// CrashDump — captured on a fatal error. Writes a JSON-formatted
// dump to crashdumps/<timestamp>.json containing backtrace, system
// info, server version, loaded plugins, etc.
#pragma once
#include <string>
namespace cursestone {
class CrashDump {
public:
    explicit CrashDump(std::string data_path) : data_path_(std::move(data_path)) {}
    void crash(const std::string& reason, const std::string& trace);
private:
    std::string data_path_;
};
} // namespace
