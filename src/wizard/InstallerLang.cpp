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
namespace cursestone::wizard {
InstallerLang& InstallerLang::get() {
    static InstallerLang i;
    return i;
}
bool InstallerLang::load(const std::string& lang_code) {
    // Only the English locale is built in.
    if (lang_code != "eng")
        return false;
    strings_ = {
        {"installer.welcome", "Welcome to CurseStone! Let's set up your server."},
        {"installer.name_question", "Server name (motd)"},
        {"installer.max_players_question", "Max players"},
        {"installer.port_question", "Server port"},
        {"installer.world_name_question", "World name"},
        {"installer.world_type_question", "World type (normal/flat/nether/void)"},
        {"installer.gamemode_question", "Default gamemode (0=survival, 1=creative, 2=adventure)"},
        {"installer.difficulty_question", "Difficulty (0-3)"},
        {"installer.whitelist_question", "Enable whitelist"},
        {"installer.done", "Setup complete! Saved cursestone.yml and server.properties."},
    };
    return true;
}
const std::string& InstallerLang::get(const std::string& key) const {
    auto it = strings_.find(key);
    return it == strings_.end() ? missing_ : it->second;
}
} // namespace
