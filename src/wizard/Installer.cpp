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
#include "Installer.h"
#include "InstallerLang.h"
#include "utils/MainLogger.hpp"
#include "utils/Config.h"
#include "utils/DefaultConfig.h"
#include <iostream>
#include <filesystem>
#include <cstdio>
namespace cursestone::wizard {
namespace fs = std::filesystem;

bool Installer::shouldRun() const {
    return !fs::exists(data_path_ + "/cursestone.yml");
}

std::string Installer::ask(const std::string& q, const std::string& def) {
    // Use fputs/fgets instead of std::cout/std::cin to avoid mixing
    // C and C++ I/O streams (async logger uses fputs on stdout).
    fputs(q.c_str(), stdout);
    if (!def.empty()) {
        fputs(" [", stdout);
        fputs(def.c_str(), stdout);
        fputs("]", stdout);
    }
    fputs(": ", stdout);
    fflush(stdout);
    char buf[1024];
    if (fgets(buf, sizeof(buf), stdin) == nullptr)
        return def;
    std::string line(buf);
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
        line.pop_back();
    }
    if (line.empty())
        return def;
    return line;
}
bool Installer::askYesNo(const std::string& q, bool def) {
    std::string s = ask(q + " (y/n)", def ? "y" : "n");
    return s == "y" || s == "Y" || s == "yes";
}
int Installer::askInt(const std::string& q, int def) {
    std::string s = ask(q, std::to_string(def));
    try {
        return std::stoi(s);
    } catch (...) {
        return def;
    }
}

bool Installer::runWizard() {
    if (!shouldRun())
        return true; // already configured
    utils::MainLogger::get().info("Welcome to CurseStone! First-run setup:");
    auto& lang = InstallerLang::get();
    lang.load("eng");
    std::string name = ask(lang.get("installer.name_question"), "CurseStone");
    int max_players = askInt(lang.get("installer.max_players_question"), 20);
    int port = askInt(lang.get("installer.port_question"), 19132);
    std::string world_name = ask(lang.get("installer.world_name_question"), "world");
    std::string world_type = ask(lang.get("installer.world_type_question"), "normal");
    int gamemode = askInt(lang.get("installer.gamemode_question"), 0);
    int difficulty = askInt(lang.get("installer.difficulty_question"), 1);
    bool whitelist = askYesNo(lang.get("installer.whitelist_question"), false);

    // Basic answers go to server.properties; the generator is an advanced
    // setting and goes to cursestone.yml.
    utils::Config props;
    props.setString("motd", name);
    props.setString("server-ip", "0.0.0.0");
    props.setInt("server-port", port);
    props.setInt("max-players", max_players);
    props.setInt("gamemode", gamemode);
    props.setInt("difficulty", difficulty);
    props.setBool("white-list", whitelist);
    props.setBool("online-mode", false);
    props.setInt("view-distance", 8);
    props.setString("world-name", world_name);
    props.setString("world-seed", "");

    utils::Config adv;
    adv.setInt("settings.async-workers", 4);
    adv.setInt("settings.autosave-ticks", 6000);
    adv.setBool("settings.advanced-log", true);
    adv.setString("world.format", "anvil");
    adv.setString("world.generator", world_type);
    adv.setInt("memory.max-mb", 512);
    adv.setInt("memory.gc-interval-ticks", 200);

    fs::create_directories(data_path_);
    bool ok = props.saveIni(data_path_ + "/server.properties") &&
              adv.saveYaml(data_path_ + "/cursestone.yml");
    if (ok)
        utils::MainLogger::get().info(lang.get("installer.done"));
    else
        utils::MainLogger::get().error("Failed to save config");
    return ok;
}

bool Installer::generateDefaultConfig() {
    return utils::writeDefaultConfigs(data_path_);
}
} // namespace
