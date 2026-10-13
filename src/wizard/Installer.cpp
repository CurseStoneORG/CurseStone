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
#include <iostream>
#include <fstream>
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
    if (fgets(buf, sizeof(buf), stdin) == nullptr) return def;
    std::string line(buf);
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
        line.pop_back();
    }
    if (line.empty()) return def;
    return line;
}
bool Installer::askYesNo(const std::string& q, bool def) {
    std::string s = ask(q + " (y/n)", def ? "y" : "n");
    return s == "y" || s == "Y" || s == "yes";
}
int Installer::askInt(const std::string& q, int def) {
    std::string s = ask(q, std::to_string(def));
    try { return std::stoi(s); } catch (...) { return def; }
}

bool Installer::runWizard() {
    if (!shouldRun()) return true;  // already configured
    utils::MainLogger::get().info("Welcome to CurseStone! First-run setup:");
    auto& lang = InstallerLang::get();
    lang.load("eng");
    std::string name     = ask(lang.get("installer.name_question"), "CurseStone");
    int max_players      = askInt(lang.get("installer.max_players_question"), 20);
    int port              = askInt(lang.get("installer.port_question"), 19132);
    std::string world_name = ask(lang.get("installer.world_name_question"), "world");
    std::string world_type = ask(lang.get("installer.world_type_question"), "normal");
    int gamemode          = askInt(lang.get("installer.gamemode_question"), 0);
    int difficulty        = askInt(lang.get("installer.difficulty_question"), 1);
    bool whitelist        = askYesNo(lang.get("installer.whitelist_question"), false);

    utils::Config cfg;
    cfg.setString("settings.language", "eng");
    cfg.setString("settings.shutdown-message", "Server closed");
    cfg.setInt("settings.async-workers", 4);
    cfg.setString("server.ip", "0.0.0.0");
    cfg.setInt("server.port", port);
    cfg.setString("server.motd", name);
    cfg.setInt("server.max-players", max_players);
    cfg.setInt("server.gamemode", gamemode);
    cfg.setInt("server.difficulty", difficulty);
    cfg.setBool("server.whitelist", whitelist);
    cfg.setBool("server.online-mode", false);
    cfg.setInt("server.view-distance", 8);
    cfg.setString("world.name", world_name);
    cfg.setString("world.format", "anvil");
    cfg.setString("world.generator", world_type);
    cfg.setInt("world.seed", 0);

    fs::create_directories(data_path_);
    bool ok = cfg.saveYaml(data_path_ + "/cursestone.yml");
    if (ok) utils::MainLogger::get().info("Saved cursestone.yml");
    else    utils::MainLogger::get().error("Failed to save config");
    return ok;
}

bool Installer::generateDefaultConfig() {
    if (fs::exists(data_path_ + "/cursestone.yml")) return true;
    fs::create_directories(data_path_);
    std::ofstream f(data_path_ + "/cursestone.yml");
    f << R"YAML(settings:
  language: "eng"
  shutdown-message: "Server closed"
  async-workers: 4

server:
  ip: "0.0.0.0"
  port: 19132
  motd: "CurseStone"
  max-players: 20
  gamemode: 0
  difficulty: 1
  whitelist: false
  online-mode: false
  view-distance: 8

world:
  name: "world"
  format: "anvil"
  generator: "normal"
  seed: 0

memory:
  max-mb: 512            # RSS budget (MB) for the global GarbageCollector
  gc-interval-ticks: 200 # ticks between GC passes (200 = 10s at 20 TPS)

network:
  compression-level: 7
  batch-threshold: 256

logging:
  level: "info"
  colored: true
)YAML";
    return f.good();
}
} // namespace
