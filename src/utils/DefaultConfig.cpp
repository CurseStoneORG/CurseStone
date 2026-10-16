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
// SPDX-License-Identifier: LGPL-3.0-or-later
// Default configuration templates and file initialization.
#include "DefaultConfig.h"

#include <filesystem>
#include <fstream>

namespace cursestone::utils {

const char* const kDefaultServerProperties = "# CurseStone server properties (basic settings).\n"
                                             "# Advanced settings live in cursestone.yml.\n"
                                             "\n"
                                             "motd=CurseStone Server\n"
                                             "server-ip=0.0.0.0\n"
                                             "server-port=19132\n"
                                             "max-players=20\n"
                                             "gamemode=0\n"
                                             "difficulty=1\n"
                                             "white-list=false\n"
                                             "online-mode=false\n"
                                             "view-distance=8\n"
                                             "world-name=world\n"
                                             "# Empty or 0 selects a random seed.\n"
                                             "world-seed=\n";

const char* const kDefaultCursestoneYml =
    "# CurseStone advanced configuration.\n"
    "# Basic settings are defined in server.properties.\n"
    "\n"
    "settings:\n"
    "  async-workers: 4       # Async task pool threads.\n"
    "  autosave-ticks: 6000   # 6000 ticks = 5 minutes at 20 TPS.\n"
    "  advanced-log: true     # Enable verbose session and packet logging.\n"
    "  plugin-instruction-limit: 1000000 # Lua instructions per call (0 = unlimited).\n"
    "  plugin-time-limit-ms: 100         # Milliseconds per call (0 = unlimited).\n"
    "  plugin-max-errors: 10             # Consecutive errors before disabling (0 = never).\n"
    "\n"
    "world:\n"
    "  format: \"anvil\"        # anvil | leveldb\n"
    "  generator: \"normal\"    # normal | flat | nether | void\n"
    "\n"
    "memory:\n"
    "  max-mb: 512            # Garbage collector memory budget in MB.\n"
    "  gc-interval-ticks: 200 # GC interval in ticks (200 = 10 seconds at 20 TPS).\n";

namespace {

// Creates a file only if it does not exist.
bool seedFile(const std::filesystem::path& path, const char* content,
              std::vector<std::string>* created) {
    std::error_code ec;
    if (std::filesystem::exists(path, ec))
        return true;
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.is_open())
        return false;
    out << content;
    out.close();
    if (out.fail())
        return false;
    if (created)
        created->push_back(path.filename().string());
    return true;
}

} // namespace

bool writeDefaultConfigs(const std::string& dataPath, std::vector<std::string>* created) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories(dataPath, ec);
    const fs::path dir(dataPath);
    const bool props = seedFile(dir / "server.properties", kDefaultServerProperties, created);
    const bool yml = seedFile(dir / "cursestone.yml", kDefaultCursestoneYml, created);
    return props && yml;
}

} // namespace cursestone::utils