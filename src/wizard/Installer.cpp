#include "Installer.h"
#include "InstallerLang.h"
#include "utils/MainLogger.hpp"
#include "utils/Config.h"
#include <iostream>
#include <fstream>
#include <filesystem>
namespace atomikpe::wizard {
namespace fs = std::filesystem;

bool Installer::shouldRun() const {
    return !fs::exists(data_path_ + "/atomikpe.yml");
}

std::string Installer::ask(const std::string& q, const std::string& def) {
    std::cout << q;
    if (!def.empty()) std::cout << " [" << def << "]";
    std::cout << ": ";
    std::string line;
    if (!std::getline(std::cin, line)) return def;
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
    utils::MainLogger::get().info("Welcome to AtomikPE! First-run setup:");
    auto& lang = InstallerLang::get();
    lang.load("eng");
    std::string name     = ask(lang.get("installer.name_question"), "AtomikPE");
    int max_players      = askInt(lang.get("installer.max_players_question"), 20);
    int port              = askInt(lang.get("installer.port_question"), 19132);
    std::string level_name = ask(lang.get("installer.level_name_question"), "world");
    std::string level_type = ask(lang.get("installer.level_type_question"), "normal");
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
    cfg.setString("level.name", level_name);
    cfg.setString("level.format", "anvil");
    cfg.setString("level.generator", level_type);
    cfg.setInt("level.seed", 0);

    fs::create_directories(data_path_);
    bool ok = cfg.saveYaml(data_path_ + "/atomikpe.yml");
    if (ok) utils::MainLogger::get().info("Saved atomikpe.yml");
    else    utils::MainLogger::get().error("Failed to save config");
    return ok;
}

bool Installer::generateDefaultConfig() {
    if (fs::exists(data_path_ + "/atomikpe.yml")) return true;
    fs::create_directories(data_path_);
    std::ofstream f(data_path_ + "/atomikpe.yml");
    f << R"YAML(settings:
  language: "eng"
  shutdown-message: "Server closed"
  async-workers: 4

server:
  ip: "0.0.0.0"
  port: 19132
  motd: "AtomikPE"
  max-players: 20
  gamemode: 0
  difficulty: 1
  whitelist: false
  online-mode: false
  view-distance: 8

level:
  name: "world"
  format: "anvil"
  generator: "normal"
  seed: 0

memory:
  max-mb: 512

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
