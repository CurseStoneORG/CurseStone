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
// SPDX-License-Identifier: LGPL-3.0-or-later
// Default configuration templates and file initialization.
#include "DefaultConfig.h"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <unordered_set>
#include <utility>

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
    "# Basic settings live in server.properties; the motd below overrides\n"
    "# the one defined there when it is set.\n"
    "\n"
    "settings:\n"
    "  motd: \"pon tu motd\"    # The server name shown in the client list.\n"
    "  async-workers: 4       # Async task pool threads.\n"
    "  autosave-ticks: 6000   # 6000 ticks = 5 minutes at 20 TPS.\n"
    "  debug-mode: false      # true = show ALL debug in the console: packet\n"
    "                         # traces ([PKT]), RakNet diagnostics ([NET], ACK\n"
    "                         # health), hexdumps and verbose gameplay traces.\n"
    "                         # false = clean console with only real messages.\n"
    "  plugin-instruction-limit: 1000000 # Lua instructions per call (0 = unlimited).\n"
    "  plugin-time-limit-ms: 100         # Milliseconds per call (0 = unlimited).\n"
    "  plugin-max-errors: 10             # Consecutive errors before disabling (0 = never).\n"
    "\n"
    "gameplay:\n"
    "  keep-inventory: false  # true = players keep their items when they die.\n"
    "  bed-spawn: true        # true = sleeping or tapping a bed sets the respawn\n"
    "                         # point there; breaking that bed restores the world spawn.\n"
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

// Splits one "key=value" line the way Config::loadIni reads them: leading
// whitespace skipped, comments and [sections] rejected, key trimmed on the
// right. Returns false for anything that is not a property line.
bool splitPropertyLine(const std::string& line, std::string& key) {
    const size_t start = line.find_first_not_of(" \t");
    if (start == std::string::npos)
        return false;
    const char c = line[start];
    if (c == '#' || c == ';' || c == '[')
        return false;
    const size_t eq = line.find('=', start);
    if (eq == std::string::npos)
        return false;
    key = line.substr(start, eq - start);
    while (!key.empty() && std::isspace((unsigned char)key.back()))
        key.pop_back();
    return !key.empty();
}

// The ordered key=default pairs of the canonical template: the template
// stays the single source of truth for both creation and completion.
std::vector<std::pair<std::string, std::string>> defaultProperties() {
    std::vector<std::pair<std::string, std::string>> out;
    std::istringstream in(kDefaultServerProperties);
    std::string line;
    while (std::getline(in, line)) {
        std::string key;
        if (!splitPropertyLine(line, key))
            continue;
        out.emplace_back(std::move(key), line.substr(line.find('=') + 1));
    }
    return out;
}

// Completes an existing server.properties so every option the server reads
// is present: values, comments and keys CurseStone does not manage (panel
// defaults) are preserved, options missing from the file are appended with
// their defaults. A file without any property line (empty or comments only)
// is replaced by the default template. A file that already carries every
// option is left byte-for-byte untouched.
bool completeServerProperties(const std::filesystem::path& path, bool* completed) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open())
        return false;
    const std::string content((std::istreambuf_iterator<char>(in)),
                              std::istreambuf_iterator<char>());
    in.close();

    const std::vector<std::pair<std::string, std::string>> defaults = defaultProperties();

    std::unordered_set<std::string> present;
    present.reserve(defaults.size() * 2);
    bool hasProperties = false;
    size_t begin = 0;
    while (begin <= content.size()) {
        const size_t end = content.find('\n', begin);
        const size_t stop = (end == std::string::npos) ? content.size() : end;
        std::string line = content.substr(begin, stop - begin);
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        std::string key;
        if (splitPropertyLine(line, key)) {
            hasProperties = true;
            present.insert(std::move(key));
        }
        if (end == std::string::npos)
            break;
        begin = end + 1;
    }

    // Match the file's own line endings so panel-managed files diff clean.
    const char* eol = (content.find("\r\n") != std::string::npos) ? "\r\n" : "\n";

    std::string updated;
    if (!hasProperties) {
        // Empty or comment-only file: the default template replaces it.
        updated.assign(kDefaultServerProperties);
    } else {
        std::string additions;
        for (const auto& kv : defaults) {
            if (present.count(kv.first))
                continue;
            additions += kv.first;
            additions += '=';
            additions += kv.second;
            additions += eol;
        }
        if (additions.empty())
            return true; // already complete: untouched (no mtime churn)
        updated.reserve(content.size() + additions.size() + 96);
        updated += content;
        if (updated.back() != '\n')
            updated += eol;
        updated += eol;
        updated += "# Added by CurseStone: options that were missing from this file.";
        updated += eol;
        updated += additions;
    }

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.is_open())
        return false;
    out << updated;
    out.close();
    if (out.fail())
        return false;
    if (completed)
        *completed = true;
    return true;
}

} // namespace

bool writeDefaultConfigs(const std::string& dataPath, std::vector<std::string>* created,
                         std::vector<std::string>* completed) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories(dataPath, ec);
    const fs::path dir(dataPath);
    const fs::path propsPath = dir / "server.properties";

    // server.properties must always end up complete: hosts (Pterodactyl and
    // friends) ship stub files with a handful of options, and an empty or
    // one-line file used to be taken as-is forever.
    bool propsCompleted = false;
    bool props = false;
    if (fs::exists(propsPath, ec)) {
        props = completeServerProperties(propsPath, &propsCompleted);
    } else {
        props = seedFile(propsPath, kDefaultServerProperties, created);
    }
    if (props && propsCompleted && completed)
        completed->push_back(propsPath.filename().string());

    const bool yml = seedFile(dir / "cursestone.yml", kDefaultCursestoneYml, created);
    return props && yml;
}

} // namespace cursestone::utils