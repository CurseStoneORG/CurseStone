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
#include "InstallerLang.h"
#include "utils/Config.h"
#include <fstream>
#include <sstream>
namespace cursestone::wizard {
InstallerLang& InstallerLang::get() { static InstallerLang i; return i; }
bool InstallerLang::load(const std::string& lang_code) {
    utils::Config cfg;
    std::string path = "resources/" + lang_code + ".ini";
    if (!cfg.loadIni(path)) return false;
    // Populate strings_ from cfg — Config doesn't expose a direct
    // iterator, so we re-load and parse here.
    std::ifstream f(path);
    std::string line;
    while (std::getline(f, line)) {
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq);
        std::string v = line.substr(eq + 1);
        strings_[k] = v;
    }
    return !strings_.empty();
}
const std::string& InstallerLang::get(const std::string& key) const {
    auto it = strings_.find(key);
    return it == strings_.end() ? missing_ : it->second;
}
} // namespace
