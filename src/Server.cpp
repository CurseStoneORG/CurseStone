/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  | \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

 * CurseStone | Open Source server Software
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
// Server singleton implementation.
#include "Server.h"
#include "MemoryManager.h"
#include "workbench/WorkbenchManager.h"
#include "diagnostics/Diagnostics.h"
#include "diagnostics/DiagnosticEngine.h"
#include "Player.h"
#include "utils/CRaii.h"
#include "utils/MainLogger.h"
#include "utils/MainLogger.hpp"
#include "utils/ThreadPort.h"
#include "utils/Config.h"
#include "utils/Utils.h"
#include "utils/TextColor.h" // isSectionAt — chat keeps the game's § colors.

#include <climits>
#include <cstring>
#include <cerrno>
#include <thread>
#include <chrono>
#include <cstdlib> // std::_Exit
#include <fstream>
#include <sstream>
#ifdef __linux__
#include <unistd.h> // sysconf(_SC_CLK_TCK) for the pregen CPU readout
#endif
#include <zlib.h>
#include "command/CommandMap.h"
#include "command/SimpleCommandMap.h"
#include "command/ConsoleCommandSender.h"
#include "command/CommandReader.h"
#include "command/Defaults.h"
#include "block/BlockRegistry.h"
#include "item/Item.h"
#include "inventory/CraftingManager.h"
#include "network/protocol/CraftingDataPacket.h"
#include "network/protocol/TextPacket.h"
#include "scheduler/AsyncPool.h"
#include "world/performance/PerformanceManager.h"
#include "world/performance/GenerationProfiler.h"
#include "world/performance/SaveProfiler.h"
#include "scheduler/ServerScheduler.h"
#include "scheduler/CallbackTask.h"
#include "world/World.h"
#include "world/ChunkManager.h"
#include "world/format/WorldProviderManager.h"
#include "event/EventManager.h"
#include "event/world/WorldLoadEvent.h"
#include "event/world/WorldUnloadEvent.h"
#include "network/Network.h"
#include "network/RakLibInterface.h"
#include "nbt/NBT.h"
#include "nbt/tag/CompoundTag.h"
#include "nbt/tag/Tag.h"
#include "nbt/tag/ByteTag.h"
#include "nbt/tag/ShortTag.h"
#include "nbt/tag/IntTag.h"
#include "nbt/tag/LongTag.h"
#include "nbt/tag/FloatTag.h"
#include "nbt/tag/DoubleTag.h"
#include "nbt/tag/StringTag.h"
#include "nbt/tag/ListTag.h"
#include "utils/BinaryStream.h"
#include "permission/BanList.h"
#include "plugin/PluginManager.h" // Lua + native plugin support
#include "plugin/LuaPlugin.h"     // plugin::LuaPlugin::limits() in the startup log
#include "utils/Config.h"
#include "utils/DefaultConfig.h"
#include "utils/DebugFlag.h"
#include "utils/Terminal.h"

#include <iostream>
#include <fstream>
#include <chrono>
#include <thread>
#include <algorithm>
#include <filesystem>
#include <sys/stat.h>
#include <sys/types.h>
#include "utils/PlatformCompat.h"
#include <cctype>

namespace cursestone {

Server::Server() = default;
Server::~Server() = default;

namespace {

// Reads a file into a string for key-span lookups (Diagnostic Engine). Empty
// when the file cannot be read — the caller decides whether that matters.
std::string slurpText(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return {};
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// Reads a boolean value, accepting true/on/yes/y/1 case-insensitively.
bool readBool(const utils::Config& cfg, const char* key, bool def) {
    std::string v = cfg.getString(key, std::string());
    if (v.empty())
        return def;
    std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) {
        return std::tolower(c);
    });
    return v == "true" || v == "on" || v == "yes" || v == "y" || v == "1";
}

// Range validation with a Diagnostic Engine report (E0003) anchored at the
// exact file:line of the offending value. Recoverable: the default is used
// and boot continues.
void diagBadValue(const std::string& file, const std::string& display, const std::string& key,
                  bool ini, const std::string& why, const std::string& fallback) {
    diagnostics::Diagnostic d;
    d.code = 3; // E0003 — configuration value invalid
    d.severity = diagnostics::DiagSeverity::Error;
    d.title = "Invalid value for '" + key + "' in " + display;
    d.label = why;
    d.help.push_back("Falling back to the default (" + fallback + ").");
    int line = 0, col = 0, span = 0;
    if (diagnostics::DiagEngine::findKeySpan(slurpText(file), key, ini, &line, &col, &span)) {
        d.file = display;
        d.line = line;
        d.col = col;
        d.span = span;
    }
    diagnostics::DiagEngine::emit(d);
}

} // namespace

bool Server::loadConfig() {
    using utils::Config;
    using diagnostics::DiagEngine;
    using diagnostics::DiagSeverity;
    Config props; // Basic server.properties settings.
    Config adv;   // Advanced cursestone.yml settings.

    const std::string props_path = dataPath_ + "/server.properties";
    const std::string yaml_path = dataPath_ + "/cursestone.yml";

    // A config file that EXISTS but cannot be loaded is essential: the
    // operator's values would be silently replaced by defaults, so boot
    // stops with a diagnostic instead. A missing file is normal (defaults
    // were just seeded by writeDefaultConfigs).
    const std::string props_text = slurpText(props_path);
    const std::string adv_text = slurpText(yaml_path);
    const bool props_exists = std::filesystem::exists(props_path);
    const bool adv_exists = std::filesystem::exists(yaml_path);

    if (props.loadIni(props_path)) {
        if (props_exists)
            DiagEngine::stageItem("server.properties loaded!");
    } else if (props_exists) {
        diagnostics::Diagnostic d;
        d.code = 1; // E0001 — configuration file cannot be read
        d.severity = DiagSeverity::Fatal;
        d.title = "Cannot read configuration file";
        d.file = "server.properties";
        d.notes.push_back("the file exists but could not be parsed or opened");
        DiagEngine::fatal(d);
        return false;
    }
    if (adv.loadYaml(yaml_path)) {
        if (adv_exists)
            DiagEngine::stageItem("cursestone.yml loaded!");
    } else if (adv_exists) {
        diagnostics::Diagnostic d;
        d.code = 1; // E0001 — configuration file cannot be read
        d.severity = DiagSeverity::Fatal;
        d.title = "Cannot read configuration file";
        d.file = "cursestone.yml";
        d.notes.push_back("the file exists but could not be parsed or opened");
        DiagEngine::fatal(d);
        return false;
    }

    // Load server.properties settings.
    ip_ = props.getString("server-ip", "0.0.0.0");
    port_ = (uint16_t)props.getInt("server-port", 19132);
    if (port_ == 0) {
        diagBadValue(props_path, "server.properties", "server-port", true, "UDP ports start at 1",
                     "19132");
        port_ = 19132;
    }
    motd_ = props.getString("motd", "CurseStone");
    max_players_ = (int)props.getInt("max-players", 20);
    if (max_players_ <= 0) {
        diagBadValue(props_path, "server.properties", "max-players", true, "must be 1 or more",
                     "20");
        max_players_ = 20;
    }
    gamemode_ = (int)props.getInt("gamemode", 0);
    if (gamemode_ < 0 || gamemode_ > 3) {
        diagBadValue(props_path, "server.properties", "gamemode", true,
                     "expected 0 (survival), 1 (creative), 2 (adventure) or 3 (spectator)", "0");
        gamemode_ = 0;
    }
    difficulty_ = (int)props.getInt("difficulty", 1);
    whitelist_ = readBool(props, "white-list", false);
    online_mode_ = readBool(props, "online-mode", false);
    view_distance_ = (int)props.getInt("view-distance", 8);
    if (view_distance_ < 2 || view_distance_ > 32) {
        diagBadValue(props_path, "server.properties", "view-distance", true,
                     "expected between 2 and 32 chunks", "8");
        view_distance_ = 8;
    }
    // Mob record ceiling. The default keeps one million records reachable
    // (records only exist for resident chunks, so the ceiling bounds
    // worst-case memory at roughly 200 bytes per record, not steady-state
    // tick cost); deployments on small hosts can lower it.
    max_mobs_ = (size_t)props.getInt("max-mobs", 1000000);
    if (max_mobs_ < 16 || max_mobs_ > 2000000) {
        diagBadValue(props_path, "server.properties", "max-mobs", true,
                     "expected between 16 and 2000000", "1000000");
        max_mobs_ = 1000000;
    }

    // Load cursestone.yml settings.
    async_workers_ = (int)adv.getInt("settings.async-workers", 8);
    // Boot-time spawn pre-generation: -1 follows view-distance, 0 disables,
    // explicit values clamp into 1..16 — big enough to cover any sane first
    // view, small enough that the background square never dominates boot.
    spawn_pregen_radius_ = (int)adv.getInt("world.spawn-pregen-radius", -1);
    if (spawn_pregen_radius_ < 0)
        spawn_pregen_radius_ = view_distance_;
    if (spawn_pregen_radius_ > 16)
        spawn_pregen_radius_ = 16;
    autosave_ticks_ = (int)adv.getInt("settings.autosave-ticks", 6000);
    // debug-mode is THE switch for all debug/diagnostic console output.
    // The legacy `advanced-log` key is still accepted so existing
    // cursestone.yml files load cleanly, but it no longer turns anything
    // on: verbose output follows debug-mode only.
    debug_mode_ = readBool(adv, "settings.debug-mode", false);
    (void)readBool(adv, "settings.advanced-log", false);
    mcp_debug_mode_set(debug_mode_); // C-shared flag (utils/DebugFlag.h)
    keep_inventory_ = readBool(adv, "gameplay.keep-inventory", false);
    bed_spawn_enabled_ = readBool(adv, "gameplay.bed-spawn", true);

    // The motd in cursestone.yml defines the server name when set;
    // server.properties is only the fallback.
    const std::string ymlMotd = adv.getString("settings.motd", "");
    if (!ymlMotd.empty())
        motd_ = ymlMotd;

    // Apply memory limits before the tick loop starts.
    MemoryManager::get().setLimitMB((int)adv.getInt("memory.max-mb", 512));
    MemoryManager::get().setGcIntervalTicks(
        (uint64_t)std::max<int64_t>(20, adv.getInt("memory.gc-interval-ticks", 200)));

    // Traffic pacing budgets (see TrafficLimits in Server.h). The gate is ON
    // by default: budgets are sized far above anything an honest client
    // produces, so the section only needs touching when a host wants to
    // tighten them (or set pacing: false to switch it off entirely).
    traffic_.pacing = readBool(adv, "traffic.pacing", true);
    traffic_.max_packets_per_tick =
        (unsigned)adv.getInt("traffic.max-packets-per-tick", 250);
    traffic_.max_pings_per_sec = (unsigned)adv.getInt("traffic.max-pings-per-sec", 30);
    traffic_.max_handshakes_per_sec =
        (unsigned)adv.getInt("traffic.max-handshakes-per-sec", 25);
    traffic_.max_new_sessions_per_sec =
        (unsigned)adv.getInt("traffic.max-new-sessions-per-sec", 6);
    traffic_.max_sessions_per_ip = (unsigned)adv.getInt("traffic.max-sessions-per-ip", 4);
    traffic_.strikes_before_pacing =
        (unsigned)std::max<int64_t>(1, adv.getInt("traffic.strikes-before-pacing", 1));
    traffic_.pacing_hold_ms = (unsigned)adv.getInt("traffic.pacing-hold-ms", 3000);
    traffic_.pacing_hold_max_ms = (unsigned)adv.getInt("traffic.pacing-hold-max-ms", 60000);
    traffic_.pacing_expire_ms = (unsigned)adv.getInt("traffic.pacing-expire-ms", 30000);

    // Plugin limits support zero to disable each limit and root-level fallbacks.
    plugin::PluginLimits pluginLimits;
    pluginLimits.instructions = adv.getInt("settings.plugin-instruction-limit",
                                           adv.getInt("plugin-instruction-limit", 1000000));
    pluginLimits.timeMs =
        adv.getInt("settings.plugin-time-limit-ms", adv.getInt("plugin-time-limit-ms", 100));
    pluginLimits.maxErrors =
        (int)adv.getInt("settings.plugin-max-errors", adv.getInt("plugin-max-errors", 10));
    plugin::LuaPlugin::setLimits(pluginLimits);

    // Config detail rides DEBUG: the Diagnostic Engine keeps the normal
    // boot console minimal; `settings.debug-mode: true` reveals everything.
    // The identity line that used to be a boot banner lives here now so the
    // normal log never repeats what "Starting server in CurseStone vX" said.
    utils::MainLogger::get().debug(
        std::string(CURSESTONE_NAME) + " v" + CURSESTONE_API + " — Minecraft PE " +
        MINECRAFT_VERSION + ", protocol " + std::to_string(PROTOCOL_VERSION));
    utils::MainLogger::get().debug(
        "Debug mode: " + std::string(debug_mode_ ? "enabled" : "disabled") +
        " (settings.debug-mode in cursestone.yml)");
    utils::MainLogger::get().debug("Server port: " + std::to_string(port_));
    utils::MainLogger::get().debug("Server IP: " + ip_);
    utils::MainLogger::get().debug("MOTD: " + motd_);
    utils::MainLogger::get().debug("Max players: " + std::to_string(max_players_));
    utils::MainLogger::get().debug("Gamemode: " + std::to_string(gamemode_) +
                                   " (0=survival, 1=creative, 2=adventure, 3=spectator)");
    utils::MainLogger::get().debug("World generator: " + adv.getString("world.generator", "normal"));
    utils::MainLogger::get().debug("World format: " + adv.getString("world.format", "anvil"));
    utils::MainLogger::get().debug(std::string("Keep inventory: ") +
                                   (keep_inventory_ ? "on" : "off"));
    utils::MainLogger::get().debug(std::string("Bed spawn: ") +
                                   (bed_spawn_enabled_ ? "on" : "off"));
    utils::MainLogger::get().debug(
        "Plugin limits: instructions=" + std::to_string(plugin::LuaPlugin::limits().instructions) +
        ", time-ms=" + std::to_string(plugin::LuaPlugin::limits().timeMs) +
        ", max-errors=" + std::to_string(plugin::LuaPlugin::limits().maxErrors) + " (0 = off)");
    utils::MainLogger::get().debug(
        std::string("Traffic pacing: ") + (traffic_.pacing ? "on" : "off") +
        " (per-tick datagrams " + std::to_string(traffic_.max_packets_per_tick) +
        ", pings/s " + std::to_string(traffic_.max_pings_per_sec) + ", handshakes/s " +
        std::to_string(traffic_.max_handshakes_per_sec) + ", new sessions/s " +
        std::to_string(traffic_.max_new_sessions_per_sec) + ", sessions/IP " +
        std::to_string(traffic_.max_sessions_per_ip) + ")");
    return true;
}

void Server::initLogger() {
    // Write server.log to the data directory in real time.
    std::string log_path = dataPath_ + "/server.log";

    // Color mode (settings.console-color): "auto" colors only when stdout is
    // a real terminal, "always" forces ANSI (the legacy behavior), "never"
    // strips every escape. Default: auto.
    utils::Config early;
    early.loadYaml(dataPath_ + "/cursestone.yml");
    std::string cmode = early.getString("settings.console-color", "auto");
    if (cmode == "never") {
        mcp_terminal_init(false);
        mcp_terminal_set_enabled(false);
    } else if (cmode == "always") {
        mcp_terminal_init(true);
    } else {
        mcp_terminal_init(false); // auto: isatty(stdout)
    }

    mcp_main_logger_init(log_path.c_str(), /*colored=*/mcp_terminal_has_color());
    // The old file-side version banner moved to the debug block in
    // loadConfig(): the normal log opens with the stage line instead and the
    // version details ride debug mode.
    utils::MainLogger::get().debug("Log file: " + log_path);
}

void Server::registerBlocksAndItems() {
    block::register_all_blocks();
    item::Item::registerAll();
    utils::MainLogger::get().debug("Block and item IDs registered");
}

bool Server::openNetwork() {
    using diagnostics::DiagEngine;
    using diagnostics::DiagSeverity;
    network_ = std::make_shared<network::Network>(this);
    auto rli = std::make_unique<network::RakLibInterface>(this);
    // Keep a raw pointer to start the interface after transferring ownership.
    auto* rli_ptr = rli.get();
    network_->registerInterface(std::move(rli));
    rli_ptr->start();
    if (!rli_ptr->isRunning()) {
        // The spec-level rule: no network, no server. RakLib already logged
        // the bind error; report it as a fatal diagnostic and abort boot.
        diagnostics::Diagnostic d;
        d.code = 3001;
        d.severity = DiagSeverity::Fatal;
        d.title = "Network interface failed to bind";
        d.notes.push_back("address " + ip_ + " port " + std::to_string(port_));
        d.help.push_back("Is another server already running on that port?");
        d.help.push_back("Change server-port in server.properties or free the port.");
        DiagEngine::fatal(d);
        network_.reset();
        return false;
    }
    DiagEngine::stage("listening on " + ip_ + ":" + std::to_string(port_));
    return true;
}

bool Server::loadDefaultWorld() {
    using diagnostics::DiagEngine;
    using diagnostics::DiagSeverity;
    // Read the world name and seed from server.properties and format/generator from cursestone.yml.
    using utils::Config;
    Config props;
    Config adv;
    props.loadIni(dataPath_ + "/server.properties");
    adv.loadYaml(dataPath_ + "/cursestone.yml");

    world::WorldConfig lc;
    lc.name = props.getString("world-name", "world");
    if (lc.name.empty())
        lc.name = "world";
    lc.format = adv.getString("world.format", "anvil");
    lc.generator = adv.getString("world.generator", "normal");
    if (lc.generator.empty())
        lc.generator = "normal";
    const int64_t seed = props.getInt("world-seed", 0);
    lc.seed = (seed != 0) ? seed : (int64_t)mcp_utils_time_ms();
    lc.spawn_x = 0;
    lc.spawn_y = 64;
    lc.spawn_z = 0;
    lc.view_distance = view_distance_;
    lc.max_mobs = max_mobs_;
    lc.dataPath = dataPath_; // Allows World to resolve absolute world paths.

    // Create world directories before constructing the WorldProvider.
    // The format's own subdirectory matches what its provider creates:
    // region/ for Anvil, db/ for LevelDB (both providers also ensure their
    // own, so this only keeps the scaffolding consistent with the config).
    std::string worldsDir = dataPath_ + "/worlds";
    std::string worldDir = worldsDir + "/" + lc.name;
    ::mkdir(worldsDir.c_str(), 0755);
    ::mkdir(worldDir.c_str(), 0755);
    std::string formatDir = (lc.format == "leveldb") ? "/db" : "/region";
    ::mkdir((worldDir + formatDir).c_str(), 0755);

    // Construct the World and initialize its provider.
    world_ = std::make_shared<world::World>(std::move(lc));
    if (!world_ || !world_->provider()) {
        // The default world IS the server: without it there is nothing to
        // spawn into, so this one failure aborts the boot.
        diagnostics::Diagnostic d;
        d.code = 2004;
        d.severity = DiagSeverity::Fatal;
        d.title = "Default world failed to load";
        d.file = "worlds/" + lc.name;
        d.help.push_back("Restore the world folder from a backup, or set world-name to a new one.");
        DiagEngine::fatal(d);
        world_.reset();
        return false;
    }
    worlds_.push_back(world_);
    DiagEngine::stage("\"" + world_->name() + "\" loaded!");
    utils::MainLogger::get().debug("World directory: " + worldDir);
    utils::MainLogger::get().debug("Level directory: " + worldDir + formatDir);

    // Secondary worlds remembered by earlier sessions reload here, before
    // plugins enable, so plugin worlds are ready when their plugins start.
    loadWorldsManifest();
    return true;
}

// ---------------------------------------------------------------------
// Boot-time spawn pre-generation.
// ---------------------------------------------------------------------
namespace {
// Worker-scaling counters sampled when the pregen square is queued and
// again when the completion report fires; the diff describes exactly the
// pregen window. Pure observers — nothing here changes scheduling.
struct PregenStatSample {
    scheduler::AsyncPool::Snapshot pool{};
    world::ChunkManager::LockStats::Snapshot chunk_lock{};
    world::ChunkManager::LockStats::Snapshot pending_lock{};
    uint64_t spill_writes = 0;
    uint64_t spill_parked = 0;
    uint64_t spill_lock_wait_ns = 0;
    uint64_t gen_total_ns = 0;
    uint64_t gen_chunks = 0;
    uint64_t cpu_ticks = 0; // utime+stime, Linux only (0 elsewhere)
};
PregenStatSample g_pregen_start;

#ifdef __linux__
// utime+stime of this process from /proc/self/stat. comm may contain
// spaces, so parse past the ')' before tokenizing.
bool readProcessCpuTicks(uint64_t& out) {
    std::ifstream f("/proc/self/stat");
    if (!f)
        return false;
    std::string data;
    if (!std::getline(f, data))
        return false;
    const size_t rparen = data.rfind(')');
    if (rparen == std::string::npos)
        return false;
    std::istringstream ss(data.substr(rparen + 1));
    // Fields 3..15 follow comm; utime is field 14 (token index 11),
    // stime field 15.
    std::string tok;
    int i = 0;
    long long utime = -1, stime = -1;
    while (ss >> tok) {
        if (i == 11)
            utime = atoll(tok.c_str());
        else if (i == 12) {
            stime = atoll(tok.c_str());
            break;
        }
        ++i;
    }
    if (utime < 0 || stime < 0)
        return false;
    out = (uint64_t)(utime + stime);
    return true;
}

uint64_t residentRssKb() {
    std::ifstream f("/proc/self/status");
    std::string line;
    while (std::getline(f, line)) {
        if (line.rfind("VmRSS:", 0) == 0)
            return (uint64_t)atoll(line.c_str() + 6);
    }
    return 0;
}

double clockTicksPerSec() {
    static const double tps = (double)sysconf(_SC_CLK_TCK);
    return tps > 0 ? tps : 100.0;
}
#else
bool readProcessCpuTicks(uint64_t&) { return false; }
uint64_t residentRssKb() { return 0; }
double clockTicksPerSec() { return 100.0; }
#endif

PregenStatSample samplePregenStats(Server& srv) {
    PregenStatSample s;
    if (srv.getAsyncPool())
        s.pool = srv.getAsyncPool()->statsSnapshot();
    if (srv.getDefaultWorld()) {
        const world::ChunkManager& cm = srv.getDefaultWorld()->chunks();
        s.chunk_lock = cm.chunkLockSnap();
        s.pending_lock = cm.pendingLockSnap();
        s.spill_writes = cm.spillWritesSnap();
        s.spill_parked = cm.spillParkedSnap();
        s.spill_lock_wait_ns = cm.spillLockWaitNsSnap();
    }
    const auto gs = performance::GenerationProfiler::peek();
    s.gen_total_ns = gs.stages[(size_t)performance::Stage::Total].ns;
    s.gen_chunks = gs.stages[(size_t)performance::Stage::Total].calls;
    readProcessCpuTicks(s.cpu_ticks);
    return s;
}
} // namespace

// Queue the spawn square onto the async pool the moment the world is up:
// terrain for the first joiner — and for the synchronous spawn settle the
// login performs — is generated while nobody is connected, so the first
// player streams into a hot world instead of watching chunks pop in.
// Vanilla's "Preparing spawn area", but off the boot thread: the server
// becomes ready immediately and the pool drains the square in the
// background while plugins load and the listener accepts.
void Server::preGenerateSpawnArea() {
    if (!world_ || !async_pool_ || spawn_pregen_radius_ <= 0)
        return;

    const mcp_vector3_t spawn = world_->spawn();
    const int scx = (int)spawn.x >> 4;
    const int scz = (int)spawn.z >> 4;

    // Center-out order, the same shape the per-player stream uses: the
    // chunks a joining player needs first are queued (and generated) first.
    auto& chunks = world_->chunks();
    size_t queued = 0;
    for (const auto& pr : Player::buildChunkSpiral(spawn_pregen_radius_)) {
        if (!chunks.getChunkOrRequestAsync(scx + pr.first, scz + pr.second,
                                           async_pool_.get()))
            ++queued; // Resident chunks return a pointer and need no work.
    }
    if (queued == 0)
        return; // Everything already resident (a warm world reloading).

    spawn_pregen_target_ = queued;
    spawn_pregen_start_ms_ = mcp_utils_time_ms();
    spawn_pregen_waiting_ = true;
    g_pregen_start = samplePregenStats(*this);
    utils::MainLogger::get().info("Preparing spawn area: " +
                                  std::to_string(queued) + " chunks (radius " +
                                  std::to_string(spawn_pregen_radius_) + ")");
}

// Completion probe for the boot pre-generation. The pool is FIFO, so once
// the last generation task has been dequeued every install has finished or
// is finishing on a worker — pendingCount() hitting zero means the square
// is fully resident. A sleeping watcher task inside the pool would block
// stop()'s synchronous drain, which is why the probe lives on the tick
// loop, polled twice per second (one small mutex grab, boot-scoped).
void Server::pollSpawnPregen() {
    if (!spawn_pregen_waiting_)
        return;
    if (currentTick_ - spawn_pregen_last_poll_tick_ < 10)
        return;
    spawn_pregen_last_poll_tick_ = currentTick_;
    if (!world_ || world_->chunks().pendingCount() > 0)
        return;

    spawn_pregen_waiting_ = false;
    const uint64_t ms = mcp_utils_time_ms() - spawn_pregen_start_ms_;
    utils::MainLogger::get().info("Spawn area pregenerated: " +
                                  std::to_string(spawn_pregen_target_) +
                                  " chunks in " + std::to_string(ms) + " ms");

    // Worker-scaling window: pool busy/idle/queue wait, chunk-store lock
    // waits, process CPU and RSS over exactly this pregen. Read before
    // logStageBreakdown drains the profiler counters.
    {
        const PregenStatSample end = samplePregenStats(*this);
        const uint64_t busy_ns = end.pool.busy_ns - g_pregen_start.pool.busy_ns;
        const uint64_t idle_ns = end.pool.idle_ns - g_pregen_start.pool.idle_ns;
        const uint64_t busy_idle = busy_ns + idle_ns;
        const uint64_t qw_ns = end.pool.queue_wait_ns - g_pregen_start.pool.queue_wait_ns;
        const uint64_t qw_n =
            end.pool.queue_wait_samples - g_pregen_start.pool.queue_wait_samples;
        const uint64_t qw_max_ns =
            end.pool.queue_wait_max_ns > g_pregen_start.pool.queue_wait_max_ns
                ? end.pool.queue_wait_max_ns
                : 0; // max is a high-water, not a diff
        const uint64_t lock_ns = end.pool.lock_wait_ns - g_pregen_start.pool.lock_wait_ns;
        const uint64_t gen_ns = end.gen_total_ns - g_pregen_start.gen_total_ns;
        const uint64_t gen_n = end.gen_chunks - g_pregen_start.gen_chunks;
        const uint64_t mtx_wait_ns =
            end.chunk_lock.wait_ns - g_pregen_start.chunk_lock.wait_ns;
        const uint64_t pending_wait_ns =
            end.pending_lock.wait_ns - g_pregen_start.pending_lock.wait_ns;
        const uint64_t spill_wait_ns =
            end.spill_lock_wait_ns - g_pregen_start.spill_lock_wait_ns;

        char line[420];
        std::snprintf(
            line, sizeof(line),
            "[Perf] pregen scaling: workers=%d chunks=%llu wall_ms=%llu "
            "chunks_per_s=%.1f gen_avg_ms=%.2f worker_busy=%.1f%% "
            "worker_idle=%.1f%% queue_wait_avg_ms=%.2f "
            "queue_wait_max_ms=%.2f pool_lock_wait_ms=%.2f",
            async_workers_, (unsigned long long)spawn_pregen_target_,
            (unsigned long long)ms,
            ms > 0 ? (double)spawn_pregen_target_ * 1000.0 / (double)ms : 0.0,
            gen_n > 0 ? (double)gen_ns / (double)gen_n / 1e6 : 0.0,
            busy_idle > 0 ? 100.0 * (double)busy_ns / (double)busy_idle : 0.0,
            busy_idle > 0 ? 100.0 * (double)idle_ns / (double)busy_idle : 0.0,
            qw_n > 0 ? (double)qw_ns / (double)qw_n / 1e6 : 0.0,
            (double)qw_max_ns / 1e6, (double)lock_ns / 1e6);
        utils::MainLogger::get().info(line);

        double cpu_pct = 0.0;
        const uint64_t rss_kb = residentRssKb();
        if (end.cpu_ticks > g_pregen_start.cpu_ticks && ms > 0) {
            const double cpu_ms =
                (double)(end.cpu_ticks - g_pregen_start.cpu_ticks) /
                clockTicksPerSec() * 1000.0;
            cpu_pct = 100.0 * cpu_ms / (double)ms;
        }
        char line2[380];
        std::snprintf(
            line2, sizeof(line2),
            "[Perf] pregen locks: store_wait_ms=%.2f pending_wait_ms=%.2f "
            "spill_writes=%llu spill_parked=%llu spill_wait_ms=%.2f "
            "cpu_percent=%.1f rss_mb=%.1f queue_depth_max=%llu",
            (double)mtx_wait_ns / 1e6, (double)pending_wait_ns / 1e6,
            (unsigned long long)(end.spill_writes - g_pregen_start.spill_writes),
            (unsigned long long)(end.spill_parked - g_pregen_start.spill_parked),
            (double)spill_wait_ns / 1e6, cpu_pct, (double)rss_kb / 1024.0,
            (unsigned long long)end.pool.depth_max);
        utils::MainLogger::get().info(line2);
    }

    // Same window, stage by stage: shows what the boot burst actually
    // spent its time on instead of a single wall-clock number.
    performance::PerformanceManager::logStageBreakdown("spawn pregen");
}

// ---------------------------------------------------------------------
// Multiworld loading and unloading during runtime.
// ---------------------------------------------------------------------
// The default world is stored in world_ and worlds_[0]; plugin worlds are stored in worlds_ too
// and remembered in worlds/worlds.manifest so they reload after a restart.

bool Server::validWorldName(const std::string& name) {
    if (name.empty() || name.size() > 64)
        return false;
    for (char c : name) {
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-'))
            return false;
    }
    return true;
}

bool Server::normalizeGenerator(std::string& generator) {
    std::transform(generator.begin(), generator.end(), generator.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return generator == "normal" || generator == "flat" || generator == "nether" ||
           generator == "void" || generator == "empty";
}

std::string Server::worldsManifestPath() const {
    return dataPath_ + "/worlds/worlds.manifest";
}

void Server::manifestAdd(const std::string& name, const std::string& generator, int64_t seed) {
    std::ifstream in(worldsManifestPath());
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.compare(0, name.size() + 1, name + ";") != 0)
            lines.push_back(line); // replace any stale entry for this name
    }
    in.close();
    lines.push_back(name + ";" + generator + ";" + std::to_string(seed));
    std::ofstream out(worldsManifestPath(), std::ios::trunc);
    if (!out)
        return;
    for (const auto& l : lines)
        out << l << '\n';
}

void Server::manifestRemove(const std::string& name) {
    std::ifstream in(worldsManifestPath());
    if (!in)
        return;
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(in, line)) {
        if (line.compare(0, name.size() + 1, name + ";") != 0)
            lines.push_back(line);
    }
    in.close();
    std::ofstream out(worldsManifestPath(), std::ios::trunc);
    if (!out)
        return;
    for (const auto& l : lines)
        out << l << '\n';
}

void Server::loadWorldsManifest() {
    std::ifstream in(worldsManifestPath());
    if (!in)
        return; // no manifest: nothing was ever loaded besides the default world
    std::string line;
    int loaded = 0;
    while (std::getline(in, line)) {
        if (line.empty())
            continue;
        // name;generator;seed — the same tokens manifestAdd wrote.
        size_t p1 = line.find(';');
        size_t p2 = (p1 == std::string::npos) ? std::string::npos : line.find(';', p1 + 1);
        if (p1 == std::string::npos || p2 == std::string::npos)
            continue;
        std::string name = line.substr(0, p1);
        std::string gen = line.substr(p1 + 1, p2 - p1 - 1);
        int64_t seed = 0;
        try {
            seed = std::stoll(line.substr(p2 + 1));
        } catch (...) {
            seed = 0;
        }
        if (loadWorld(name, gen, seed))
            ++loaded;
    }
    (void)loaded; // every restored world already printed its own stage item
}

World* Server::loadWorld(const std::string& name, const std::string& generator, int64_t seed) {
    using diagnostics::DiagEngine;
    using diagnostics::DiagSeverity;
    // A world name becomes a directory name verbatim; anything that could
    // climb out of worlds/ (or produce weird region files) is refused with
    // a logged reason instead of silently creating paths elsewhere.
    if (!validWorldName(name)) {
        diagnostics::Diagnostic d;
        d.code = 2001;
        d.severity = DiagSeverity::Error;
        d.title = "Failed to load world '" + name + "'";
        d.notes.push_back("rejected world name (use 1..64 letters, digits, '_' or '-')");
        DiagEngine::emit(d);
        return nullptr;
    }
    std::string gen = generator.empty() ? "normal" : generator;
    if (!normalizeGenerator(gen)) {
        diagnostics::Diagnostic d;
        d.code = 2002;
        d.severity = DiagSeverity::Error;
        d.title = "Failed to load world '" + name + "'";
        d.notes.push_back("unknown generator '" + generator + "'");
        DiagEngine::emit(d);
        return nullptr;
    }
    // Return the existing world without modifying it.
    if (World* existing = getWorldByName(name))
        return existing;

    world::WorldConfig lc;
    lc.name = name;
    lc.format = "anvil";
    lc.generator = gen;
    lc.seed = (seed != 0) ? seed : (int64_t)mcp_utils_time_ms();
    lc.view_distance = view_distance_;
    lc.max_mobs = max_mobs_;
    lc.dataPath = dataPath_;

    // Create the required directories before provider initialization.
    std::string worldsDir = dataPath_ + "/worlds";
    std::string worldDir = worldsDir + "/" + lc.name;
    std::string regionDir = worldDir + "/region";
    ::mkdir(worldsDir.c_str(), 0755);
    ::mkdir(worldDir.c_str(), 0755);
    ::mkdir(regionDir.c_str(), 0755);

    auto lv = std::make_shared<world::World>(std::move(lc));
    if (!lv)
        return nullptr;
    worlds_.push_back(lv);
    DiagEngine::stage("\"" + name + "\" loaded!");
    utils::MainLogger::get().debug("World '" + name + "' generator=" + lv->config().generator +
                                   " dimension=" + std::to_string(lv->dimension()));
    // Remember the world across restarts — but never the default world, which
    // comes from server.properties and must not shadow its own config.
    bool is_default = world_ && lv.get() == world_.get();
    if (!is_default)
        manifestAdd(name, lv->config().generator, lv->seed());
    event::EventManager::get().fire<event::world::WorldLoadEvent>(lv.get(), 0, 0);
    return lv.get();
}

bool Server::unloadWorld(const std::string& name) {
    for (auto it = worlds_.begin(); it != worlds_.end(); ++it) {
        World* w = it->get();
        if (!w || w->name() != name)
            continue;
        if (world_ && w == world_.get()) {
            utils::MainLogger::get().warn("Cannot unload the default world '" + name + "'.");
            return false;
        }
        auto ev = event::EventManager::get().fire<event::world::WorldUnloadEvent>(w);
        if (ev.isCancelled())
            return false;

        // Move players to the default world's spawn before releasing the world.
        for (auto& p : playersSnapshotRef()) {
            if (p && p->getWorld() == w) {
                auto sp = world_ ? world_->spawn() : mcp_v3(0, 64, 0);
                p->switchWorld(world_.get(), sp.x, sp.y, sp.z, false);
            }
        }

        w->save();

        // The generation-completion callback captures the ChunkManager, so
        // destroying the world while tasks are still in flight would be a
        // use-after-free. Wait (bounded) for them to drain, otherwise refuse.
        auto genDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (w->chunks().pendingCount() > 0) {
            if (std::chrono::steady_clock::now() > genDeadline) {
                utils::MainLogger::get().warn("World '" + name +
                                              "' still has chunks generating; unload refused.");
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        worlds_.erase(it);
        manifestRemove(name);
        utils::MainLogger::get().info("World '" + name + "' unloaded.");
        return true;
    }
    return false;
}

std::vector<World*> Server::getWorlds() const {
    std::vector<World*> out;
    out.reserve(worlds_.size());
    for (auto& w : worlds_)
        if (w)
            out.push_back(w.get());
    return out;
}

std::shared_ptr<World> Server::getWorldShared(const std::string& name) const {
    for (auto& w : worlds_) {
        if (w && w->name() == name) {
            return w;
        }
    }
    return nullptr;
}

void Server::startCommandReader() {
    console_reader_ = std::make_unique<CommandReader>([this](const std::string& line) {
        if (line.empty())
            return;
        // Dispatch on the main tick thread: commands mutate players, worlds
        // and plugin VMs that the tick loop also touches. Running them on
        // the console thread raced every one of those subsystems.
        auto task = std::make_shared<scheduler::CallbackTask>([this, line]() {
            ConsoleCommandSender sender;
            if (command_map_)
                command_map_->dispatch(&sender, line);
        });
        scheduler::ServerScheduler::get().scheduleSync(task);
    });
    console_reader_->start();
}

void Server::start(const std::string& dataPath) {
    using diagnostics::DiagEngine;
    // Record startup time for the server startup log.
    auto boot_start = std::chrono::steady_clock::now();

    dataPath_ = dataPath;
    running_.store(true, std::memory_order_relaxed);

    // Record startup time for crash diagnostics.
    diagnostics::Diagnostics::get().setUptimeStart(
        (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());

    // Ensure the data directory exists.
    std::error_code ec;
    std::filesystem::create_directories(dataPath_, ec);

    DiagEngine::stage(std::string("Starting server in ") + CURSESTONE_NAME + " v" +
                      CURSESTONE_API + "...");

    // Initialize logging before loading configuration.
    initLogger();

    // Create any missing configuration files from built-in defaults and
    // complete existing ones that lack options (panel stubs, old files).
    std::vector<std::string> created;
    std::vector<std::string> completed;
    if (!utils::writeDefaultConfigs(dataPath_, &created, &completed)) {
        utils::MainLogger::get().warn("Could not create default config files in " + dataPath_);
    }
    for (const auto& name : created) {
        utils::MainLogger::get().debug("Created default " + name);
    }
    for (const auto& name : completed) {
        utils::MainLogger::get().debug("Completed " + name + " with the missing default options");
    }

    // ---- Configuration. An essential failure (a file that exists but
    //      cannot be read) aborts the boot inside loadConfig; recoverable
    //      value problems are reported and fall back to defaults.
    DiagEngine::stage("Loading config files...");
    if (!loadConfig()) {
        running_.store(false, std::memory_order_relaxed);
        start_failed_.store(true, std::memory_order_relaxed);
        return;
    }
    loadOps();
    DiagEngine::stageDone("Config files loaded!");

    // ---- Blocks/items/commands/recipes: internal machinery, visible only
    //      in debug mode.
    registerBlocksAndItems();
    command_map_ = std::make_shared<SimpleCommandMap>();
    registerDefaultCommands(*command_map_);
    crafting_ = std::make_shared<inventory::CraftingManager>();
    // Built-in recipes are registered in CraftingManager::registerBuiltinRecipes().
    int recipeCount = crafting_->registerBuiltinRecipes();
    utils::MainLogger::get().debug("Loaded " + std::to_string(recipeCount) + " crafting recipes");
    async_pool_ = std::make_shared<scheduler::AsyncPool>(async_workers_);

    // Performance module: CPU detection + noise kernel dispatch decisions
    // before the worlds (and their generators) come up.
    performance::PerformanceManager::init();

    // ---- Network. The spec-level rule: if the interface cannot bind, the
    //      boot aborts (a server without a network is a zombie).
    DiagEngine::stage("Network listening...");
    if (!openNetwork()) {
        running_.store(false, std::memory_order_relaxed);
        start_failed_.store(true, std::memory_order_relaxed);
        return;
    }

    // ---- Worlds. The default world failing is fatal (nothing to spawn
    //      into); secondary worlds fail individually and boot continues.
    DiagEngine::stage("Loading worlds...");
    if (!loadDefaultWorld()) {
        running_.store(false, std::memory_order_relaxed);
        start_failed_.store(true, std::memory_order_relaxed);
        return;
    }
    DiagEngine::stageDone("Worlds loaded!");

    // Background pre-generation of the spawn square — queued before the
    // plugins load, finished long before the first player can join.
    preGenerateSpawnArea();

    // ---- Plugins. A failing plugin is never fatal: it is reported with a
    //      diagnostic and everything else keeps loading.
    DiagEngine::stage("Loading plugins...");
    plugin::PluginManager::get().loadAll(dataPath_ + "/plugins", dataPath_ + "/data_pl");
    DiagEngine::stageDone("Plugins loaded");

    startCommandReader();

    // Calculate total startup time. Microsecond resolution so the ready
    // epilogue can show the cost with decimals ("(0.47ms)") instead of the
    // truncated integer milliseconds.
    auto boot_end = std::chrono::steady_clock::now();
    const double boot_ms =
        std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(boot_end - boot_start)
            .count();
    DiagEngine::ready(boot_ms);
}

void Server::runTickLoop() {
    mcp_set_thread_name("CS-Main");
    uint64_t last_tick_count = 0;
    auto last_tps_time = std::chrono::steady_clock::now();
    // FASE L stop watchdog: a normal thread that reacts to the stop flag
    // even when the main thread is wedged (see the header note).
    stop_watchdog_running_.store(true, std::memory_order_relaxed);
    stop_watchdog_ = std::thread([this] { runStopWatchdog(); });
    // Use absolute tick deadlines to prevent timing drift.
    // Advance by 50 ms per tick and compensate for overruns on the next tick.
    auto next_tick = std::chrono::steady_clock::now();
    while (running_.load(std::memory_order_relaxed)) {
        // Handle external stop requests on the main thread.
        if (external_stop_flag_ && *external_stop_flag_ &&
            running_.load(std::memory_order_relaxed)) {
            shutdown();
            break;
        }
        auto start_ts = std::chrono::steady_clock::now();
        tick();
        // MSPT — the tick's own cost, measured (not guessed). Feeds the
        // /status "Tick" line: at 0-50 ms the deadline is held, at >50 ms
        // the loop physically cannot deliver 20 ticks per second. The EMA
        // (alpha 1/20) smooths single-tick spikes into a readable mean.
        const double tick_ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start_ts)
                .count();
        const double prev = mspt_.load(std::memory_order_relaxed);
        mspt_.store(prev > 0.0 ? prev + (tick_ms - prev) / 20.0 : tick_ms,
                    std::memory_order_relaxed);
        // Freeze detector: keep the worst single tick since boot. One
        // monotonic max-store; no locking, no decay — the spike must
        // survive until someone reads /status.
        const double prev_peak = mspt_peak_.load(std::memory_order_relaxed);
        if (tick_ms > prev_peak)
            mspt_peak_.store(tick_ms, std::memory_order_relaxed);
        next_tick += std::chrono::milliseconds(CURSESTONE_TICK_MS);
        if (std::chrono::steady_clock::now() < next_tick) {
            std::this_thread::sleep_until(next_tick);
        } else {
            // Re-anchor after an overrun to prevent chained catch-up ticks.
            next_tick = std::chrono::steady_clock::now();
        }
        // Update TPS once per second.
        if (currentTick_ - last_tick_count >= 20) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed_sec =
                std::chrono::duration_cast<std::chrono::milliseconds>(now - last_tps_time).count();
            if (elapsed_sec > 0) {
                // Round, don't truncate: the raw figure is 20000/elapsed,
                // so a window that slipped just 5 ms (0.25 ms/tick of
                // scheduler noise) reads 19.9 — truncation reported that
                // as a full lost tick while no tick was actually lost.
                // Real sustained loss (>= half a tick per second) still
                // shows; mspt above exposes the actual cost either way.
                tps_ = (int)(20000.0 / (double)elapsed_sec + 0.5);
            }
            last_tps_time = now;
            last_tick_count = currentTick_;
        }
    }
    utils::MainLogger::get().info("Tick loop exited.");
    tick_loop_exited_.store(true, std::memory_order_relaxed);
    // Disable plugins after the tick loop exits.
    plugin::PluginManager::get().disableAll();

    // Run cleanup on the main thread after the tick loop exits.
    if (network_)
        network_->tick(); // Gracefully close RakLib.
    // Stop and join the network thread now, not at static destruction: it
    // still logs while draining, and anything it logs after main() destroys
    // the async logger would silently respawn the logger mid-shutdown.
    if (network_)
        network_->shutdown();
    // Force a final GC pass before saving all worlds.
    MemoryManager::get().collect(/*force=*/true);
    saveAll();
    if (console_reader_)
        console_reader_->stop();

    // Stop and join chunk-generation workers before global data is destroyed.
    if (async_pool_)
        async_pool_->stop();

    utils::MainLogger::get().info("Shutdown complete.");

    // Stop the stop watchdog LAST: it monitors the shutdown cleanup too
    // (the save-all below the tick loop is itself a long stretch that
    // used to look dead from the outside).
    stop_watchdog_running_.store(false, std::memory_order_relaxed);
    if (stop_watchdog_.joinable())
        stop_watchdog_.join();
}

void Server::tick() {
    ++currentTick_;
    diag_tick_.store(currentTick_, std::memory_order_relaxed);

    // Update the current tick for crash diagnostics.
    diagnostics::Diagnostics::get().setTick(currentTick_);

    // Run the Workbench tick-start hook.
    auto wb_start = std::chrono::steady_clock::now();
    WorkbenchManager::get().onTickStart();

    // Remove players marked for deletion.
    {
        std::lock_guard<std::mutex> lk(players_mtx_);
        auto it = std::remove_if(players_.begin(), players_.end(),
                                 [](const std::shared_ptr<Player>& p) {
                                     return !p || p->isPendingRemoval();
                                 });
        if (it != players_.end()) {
            players_.erase(it, players_.end());
            // Invalidate the playersSnapshotRef() cache; the next read
            // rebuilds it once instead of copying per broadcast call.
            players_version_.fetch_add(1, std::memory_order_relaxed);
        }
    }

    // Tick the scheduler to process synchronous tasks and async completions.
    auto t0 = std::chrono::steady_clock::now();
    scheduler::ServerScheduler::get().tick(currentTick_);
    auto t1 = std::chrono::steady_clock::now();
    WorkbenchManager::get().onTickPhase(
        "scheduler",
        (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count());

    // Spawn pre-generation completion probe (boot-scoped, twice a second).
    pollSpawnPregen();

    // Tick all loaded worlds.
    t0 = std::chrono::steady_clock::now();
    for (auto& w : worlds_) {
        if (w) {
            auto w0 = std::chrono::steady_clock::now();
            w->tick(currentTick_);
            w->beginTickSample(std::chrono::duration<double, std::milli>(
                                   std::chrono::steady_clock::now() - w0)
                                   .count());
        }
    }
    t1 = std::chrono::steady_clock::now();
    WorkbenchManager::get().onTickPhase(
        "level", (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count());

    // Tick block entities separately to isolate packet sends.
    t0 = std::chrono::steady_clock::now();
    for (auto& w : worlds_) {
        if (w) {
            auto w0 = std::chrono::steady_clock::now();
            w->tickTiles(currentTick_);
            w->addTickSample(std::chrono::duration<double, std::milli>(
                                 std::chrono::steady_clock::now() - w0)
                                 .count());
        }
    }
    t1 = std::chrono::steady_clock::now();
    WorkbenchManager::get().onTickPhase(
        "tiles", (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count());

    // Run the centralized garbage collection pass.
    t0 = std::chrono::steady_clock::now();
    MemoryManager::get().collect();
    t1 = std::chrono::steady_clock::now();
    WorkbenchManager::get().onTickPhase(
        "gc", (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count());

    // Process network input before updating players.
    t0 = std::chrono::steady_clock::now();
    if (network_)
        network_->tick();
    t1 = std::chrono::steady_clock::now();
    WorkbenchManager::get().onTickPhase(
        "network",
        (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count());

    // Record the main thread's latest activity time.
    WorkbenchManager::get().last_drain_time_ms.store(
        (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count(),
        std::memory_order_relaxed);

    // Update online players.
    const auto& snap = playersSnapshotRef();
    for (auto& p : snap) {
        if (!p)
            continue;
        if (p->isPendingRemoval())
            continue;
        auto pt0 = std::chrono::steady_clock::now();
        p->onUpdate(currentTick_);
        auto pt1 = std::chrono::steady_clock::now();
        WorkbenchManager::get().onPlayerUpdate(
            p->getName(),
            (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(pt1 - pt0).count());
    }

    // Refresh the player snapshot for crash diagnostics twice per second.
    if (currentTick_ % 10 == 0) {
        uint64_t now_wall = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::system_clock::now().time_since_epoch())
                                .count();
        std::vector<diagnostics::PlayerSnapshot> ps;
        ps.reserve(snap.size());
        for (auto& p : snap) {
            if (!p)
                continue;
            diagnostics::PlayerSnapshot s;
            s.name = p->getName();
            s.ip = p->ip();
            s.port = p->port();
            s.eid = p->runtimeId();
            auto pos = p->getNextPosition();
            s.x = pos.x;
            s.y = pos.y;
            s.z = pos.z;
            s.cx = p->chunkX();
            s.cz = p->chunkZ();
            s.gamemode = p->gamemode();
            s.connected = p->isConnected();
            s.logged_in = p->loggedIn();
            s.spawned = p->spawned();
            s.open_container = p->hasOpenContainer();
            uint64_t la = p->lastActivityMs();
            s.ms_since_activity = (la > 0 && now_wall > la) ? (now_wall - la) : 0;
            ps.push_back(std::move(s));
        }
        diagnostics::Diagnostics::get().updatePlayers(std::move(ps));
    }

    // Flush each player's queued packets as a compressed batch.
    {
        auto bpf0 = std::chrono::steady_clock::now();
        for (auto& p : snap) {
            if (!p)
                continue;
            if (p->isPendingRemoval())
                continue;
            p->flushPendingPackets();
        }
        auto bpf1 = std::chrono::steady_clock::now();
        WorkbenchManager::get().onTickPhase(
            "batch-flush",
            (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(bpf1 - bpf0).count());
    }

    // Flush outbound network traffic.
    if (network_)
        network_->tick();

    // Save player data and drain dirty chunks incrementally.
    if (autosave_ticks_ > 0 && currentTick_ % autosave_ticks_ == 0) {
        const auto& psnap = playersSnapshotRef();
        for (auto& p : psnap) {
            if (p)
                p->saveData(); // Save player NBT data.
        }
        // An autosave does not interrupt a manual save already in flight:
        // the drain is shared, the manual requester still gets its
        // completion message, and chunks the autosave would have written
        // ride the same budget anyway.
        if (!autosave_draining_)
            beginAutosaveDrain();
    }
    if (autosave_draining_)
        tickAutosaveDrain();

    // Run the Workbench tick-end hook.
    auto wb_end = std::chrono::steady_clock::now();
    uint64_t total_ms =
        (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(wb_end - wb_start).count();
    WorkbenchManager::get().onTickEnd(total_ms);
}

// -----------------------------------------------------------------------
// FASE L stop watchdog (L3). Signal handlers stay async-signal-safe:
// they only flip a flag. This normal thread provides what the flag alone
// cannot — operator feedback while the main thread is wedged, periodic
// diagnostics of the save pipeline, and a fully-disclosed last-resort
// exit after two minutes of zero progress. It never hides a slow disk:
// every step is logged, and the escalation reports exactly what would
// be lost (queued-but-unwritten chunk saves).
// -----------------------------------------------------------------------
void Server::runStopWatchdog() {
    mcp_set_thread_name("CS-StopWatch");
    uint64_t last_tick = 0;
    uint64_t last_written = 0;
    uint64_t last_queue = 0;
    auto last_progress = std::chrono::steady_clock::now();
    auto last_report = last_progress - std::chrono::seconds(10);
    bool announced = false;
    bool announced_main_exit = false;

    for (;;) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        if (!stop_watchdog_running_.load(std::memory_order_relaxed))
            return; // shutdown finished normally
        if (!(external_stop_flag_ && *external_stop_flag_))
            continue; // no stop requested: pure observer

        const auto now = std::chrono::steady_clock::now();
        if (!announced) {
            announced = true;
            utils::MainLogger::get().info(
                "[STOP] Shutdown requested (Ctrl+C/SIGTERM) — waiting for the main "
                "loop to reach a safe point. Progress will be reported here.");
            last_progress = now;
        }

        // Progress = ticks advancing OR the save pipeline moving (a
        // shutdown save-all freezes the tick counter while chunks are
        // still being written — that is progress, not a stall).
        const uint64_t tick = diag_tick_.load(std::memory_order_relaxed);
        const auto sp = performance::SaveProfiler::peek();
        if (tick != last_tick || sp.chunks_written != last_written ||
            sp.queue_depth != last_queue) {
            last_tick = tick;
            last_written = sp.chunks_written;
            last_queue = sp.queue_depth;
            last_progress = now;
        }
        const double stalled_s =
            std::chrono::duration<double>(now - last_progress).count();
        const bool main_exited = tick_loop_exited_.load(std::memory_order_relaxed);

        if (now - last_report >= std::chrono::seconds(10)) {
            last_report = now;
            if (main_exited) {
                if (!announced_main_exit) {
                    announced_main_exit = true;
                    utils::MainLogger::get().info(
                        "[STOP] Main loop exited — running shutdown cleanup "
                        "(save-all, provider flush, worker joins)...");
                }
            } else if (stalled_s > 5.0) {
                utils::MainLogger::get().warn(
                    "[STOP] Main loop has not made progress for " +
                    std::to_string((int)stalled_s) + "s — it is most likely wedged "
                    "inside a disk operation (save pipeline: queue=" +
                    std::to_string(sp.queue_depth) + " pending, " +
                    std::to_string(sp.chunks_written) + " written so far). "
                    "The process still honors this watchdog; nothing is being "
                    "hidden.");
            }
        }

        // Last resort: two minutes of TOTAL stall (no ticks, no writes,
        // no queue movement). Escalate WITH disclosure — the queued-but-
        // unwritten chunk saves are listed before the exit.
        if (stalled_s >= 120.0) {
            utils::MainLogger::get().error(
                "[STOP] No progress for " + std::to_string((int)stalled_s) +
                "s — forcing exit to honor the stop request. " +
                std::to_string(sp.queue_depth) + " chunk save(s) were queued but "
                "not written and are lost; " + std::to_string(sp.chunks_written) +
                " chunk(s) were written and are durable in the world db (WAL "
                "replay recovers them on the next boot). This path should never "
                "fire on a healthy disk.");
            std::_Exit(1);
        }
    }
}

void Server::shutdown() {
    // Defer cleanup until the tick loop exits to avoid cross-thread races.
    if (running_.exchange(false, std::memory_order_relaxed)) {
        utils::MainLogger::get().info("Server shutting down...");
        std::thread([this]() {
            mcp_set_thread_name("CS-Shutdown");
            // Allow five seconds for the main loop to exit cleanly.
            for (int i = 0; i < 50; ++i) {
                if (!running_.load(std::memory_order_relaxed) &&
                    tick_loop_exited_.load(std::memory_order_relaxed)) {
                    return; // Main loop exited cleanly.
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            // Force process exit if the main loop remains stuck.
            utils::MainLogger::get().error("Shutdown watchdog: tick loop did not exit within 5s — "
                                           "forcing process exit (unsaved chunks may be lost).");
            std::_Exit(1);
        }).detach();
    }
}

Player* Server::getPlayer(const std::string& name) const {
    const auto& snap = playersSnapshotRef();
    for (const auto& p : snap)
        if (p && p->getName() == name)
            return p.get();
    return nullptr;
}
void Server::addPlayer(std::shared_ptr<Player> p) {
    std::lock_guard<std::mutex> lk(players_mtx_);
    players_.push_back(std::move(p));
    players_version_.fetch_add(1, std::memory_order_relaxed);
}
void Server::removePlayer(Player* p) {
    std::lock_guard<std::mutex> lk(players_mtx_);
    auto it = std::remove_if(players_.begin(), players_.end(),
                             [p](const std::shared_ptr<Player>& q) {
                                 return q.get() == p;
                             });
    if (it != players_.end()) {
        players_.erase(it, players_.end());
        players_version_.fetch_add(1, std::memory_order_relaxed);
    }
}
std::vector<std::shared_ptr<Player>> Server::playersSnapshot() const {
    std::lock_guard<std::mutex> lk(players_mtx_);
    return players_;
}

const std::vector<std::shared_ptr<Player>>& Server::playersSnapshotRef() const {
    // Main-thread fast path: hand out the cached list and rebuild it only
    // when players_ actually changed since the last read. Mutations happen
    // in the tick-start reap and in addPlayer (network drain), both under
    // players_mtx_ and both bumping players_version_ - so a mismatch here
    // means this is the first read after a change. Steady state (no joins,
    // no leaves): no copy, no lock and no allocation for every broadcast.
    uint64_t v = players_version_.load(std::memory_order_relaxed);
    if (player_snapshot_cache_version_ != v) {
        std::lock_guard<std::mutex> lk(players_mtx_);
        v = players_version_.load(std::memory_order_relaxed);
        player_snapshot_cache_ = players_;
        player_snapshot_cache_version_ = v;
    }
    return player_snapshot_cache_;
}
void Server::broadcastMessage(const std::string& msg) {
    // Serialize the chat TextPacket once and reuse the encoded bytes for
    // every player instead of re-encoding the same payload per recipient.
    network::protocol::TextPacket pk;
    pk.type = network::protocol::TextPacket::TYPE_RAW;
    pk.message = msg;
    std::vector<uint8_t> body;
    pk.writePacket(body);
    if (!body.empty()) {
        const auto& snap = playersSnapshotRef();
        for (auto& p : snap)
            if (p)
                p->sendEncodedBody(body, pk.pid());
    }
    // Mirror broadcasts to the console keeping the game's § codes (cross-chat
    // color): the colored console converts them to ANSI, while the log file
    // and the non-colored console strip them in the logger. Only skip the
    // mirror when the text carries nothing but format codes.
    bool onlyCodes = true;
    for (size_t i = 0; i < msg.size();) {
        if (TC::isSectionAt(msg, i)) {
            i += 3;
        } else {
            onlyCodes = false;
            break;
        }
    }
    if (!onlyCodes)
        utils::MainLogger::get().chat(msg);
}
void Server::broadcastTranslation(const std::string& key,
                                  const std::vector<std::string>& parameters,
                                  const std::string& consoleFallback,
                                  const std::string& prefix) {
    // Serialize the translation TextPacket once and reuse the encoded bytes
    // for every player, exactly like broadcastMessage() — one packet body,
    // zero per-recipient re-encoding.
    network::protocol::TextPacket pk;
    pk.type = network::protocol::TextPacket::TYPE_TRANSLATION;
    pk.message = prefix + key;
    pk.parameters = parameters;
    std::vector<uint8_t> body;
    pk.writePacket(body);
    if (!body.empty()) {
        const auto& snap = playersSnapshotRef();
        for (auto& p : snap)
            if (p)
                p->sendEncodedBody(body, pk.pid());
    }
    // The console has no language table: mirror the plain-English fallback
    // so the log stays readable instead of showing raw lang keys.
    if (!consoleFallback.empty())
        utils::MainLogger::get().chat(consoleFallback);
}
int64_t Server::allocateEid() {
    return next_eid_.fetch_add(1, std::memory_order_relaxed);
}
void Server::saveAll() {
    // Save player data first so world changes are marked dirty.
    const auto& snap = playersSnapshotRef();
    for (auto& p : snap) {
        if (p)
            p->saveData();
    }
    utils::MainLogger::get().info("[SAVE] " + std::to_string(snap.size()) + " player(s) saved.");

    // Save all loaded worlds and their dirty chunks.
    for (auto& w : worlds_) {
        if (!w)
            continue;
        int saved = w->save();
        utils::MainLogger::get().info("[SAVE] World '" + w->name() + "' saved (" +
                                      std::to_string(saved) + " chunks flushed).");
    }
}

static uint64_t nowSteadyMs() {
    return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

// -----------------------------------------------------------------------
// FASE L — the shared save drain (autosave + manual /save).
//
// One budgeted pipeline, two entry points: the autosave begins it on its
// interval; requestFullSave() begins (or upgrades) it for /save with
// progress reporting and an honest completion message. The main thread
// never serializes more than kAutosaveChunksPerTick chunks per tick and
// never waits on the save worker: everything else rides the queue.

// Tell the /save requester (and the log) about save progress. The
// requester is a player name or "CONSOLE"; a player that has left just
// misses the message — the log keeps it.
void Server::notifySaveRequester(const std::string& msg) {
    utils::MainLogger::get().info(msg);
    if (autosave_drain_requester_.empty() || autosave_drain_requester_ == "CONSOLE")
        return;
    if (Player* p = getPlayerExact(autosave_drain_requester_))
        p->sendMessage(msg);
}

void Server::notifySaveRequester(const std::string& msg, const std::string& langKey) {
    utils::MainLogger::get().info(msg);
    if (autosave_drain_requester_.empty() || autosave_drain_requester_ == "CONSOLE")
        return;
    if (Player* p = getPlayerExact(autosave_drain_requester_))
        p->sendTranslation(langKey);
}

void Server::requestFullSave(const std::string& requester) {
    // Player data first (cheap, N small files) so world changes the
    // players caused are marked dirty before the pending count is taken.
    const auto& snap = playersSnapshotRef();
    for (auto& p : snap) {
        if (p)
            p->saveData();
    }

    if (autosave_draining_) {
        // Coalesce: one drain at a time. Upgrade in place and keep the
        // accounting (written/ticks) intact so the completion message
        // covers the whole drain, not just this request's tail.
        autosave_drain_manual_ = true;
        autosave_drain_requester_ = requester;
        autosave_drain_last_progress_ms_ = nowSteadyMs();
        size_t pending = 0;
        for (auto& w : worlds_) {
            if (w)
                pending += w->unsavedChunkCount();
        }
        notifySaveRequester("[SAVE] Already saving — upgraded to a full save; " +
                            std::to_string(pending) + " chunk(s) still pending.");
        return;
    }

    autosave_drain_manual_ = true;
    autosave_drain_requester_ = requester;
    autosave_drain_last_progress_ms_ = 0; // begin() sets it
    beginAutosaveDrain();
    size_t pending = 0;
    for (auto& w : worlds_) {
        if (w)
            pending += w->unsavedChunkCount();
    }
    if (autosave_draining_)
        notifySaveRequester("[SAVE] Saving world: " + std::to_string(pending) +
                                " chunk(s) pending — progress will be reported.",
                            "commands.save.start");
    else {
        // Even with zero modified chunks, /save is a full save point:
        // rewrite level.dat so Time/LastPlayed reflect the request and
        // report completion honestly.
        for (auto& w : worlds_) {
            if (w)
                w->saveBudget(/*max_chunks=*/0, /*max_ms=*/0,
                              /*write_level_data=*/true);
        }
        notifySaveRequester("[SAVE] World saved: 0 modified chunks; level.dat updated.",
                            "commands.save.success");
    }
}

// Incremental autosave pipeline.
static constexpr size_t kAutosaveChunksPerTick = 64;
static constexpr uint64_t kAutosaveMsPerTick = 3;
// Stop retrying after this many consecutive idle ticks.
static constexpr int kAutosaveIdleTicksMax = 40;

void Server::beginAutosaveDrain() {
    autosave_draining_ = false;
    if (worlds_.empty())
        return;

    autosave_drain_start_ms_ = nowSteadyMs();
    autosave_drain_written_ = 0;
    autosave_drain_ticks_ = 0;
    autosave_drain_max_tick_ms_ = 0;
    autosave_drain_idle_ticks_ = 0;

    // The manual (/save) drain writes level.dat at the END — the final
    // time and spawn values, after every chunk landed. The autosave keeps
    // its periodic level-dat write here (it has no completion step).
    if (!autosave_drain_manual_) {
        auto t0 = std::chrono::steady_clock::now();
        for (auto& w : worlds_) {
            if (w)
                w->saveBudget(/*max_chunks=*/0, /*max_ms=*/0,
                              /*write_level_data=*/true);
        }
        WorkbenchManager::get().onAutosave(
            (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - t0)
                .count());
    }
    autosave_drain_last_progress_ms_ = nowSteadyMs();

    size_t pending = 0;
    for (auto& w : worlds_) {
        if (w)
            pending += w->unsavedChunkCount();
    }
    if (pending == 0)
        return; // No dirty chunks to drain.

    autosave_draining_ = true;
    if (autosave_drain_manual_) {
        // The requester already got the "saving world" line from
        // requestFullSave(); keep the drain log for the record.
        utils::MainLogger::get().info("[SAVE] Manual save drain started: " +
                                      std::to_string(pending) + " chunk(s), budgeted at " +
                                      std::to_string(kAutosaveChunksPerTick) +
                                      "/tick (max " + std::to_string(kAutosaveMsPerTick) +
                                      "ms/tick) — the main thread never blocks.");
    } else if (debug_mode_) {
        utils::MainLogger::get().info("[SAVE] Autosave: " + std::to_string(pending) +
                                      " chunk(s) queued -> draining " +
                                      std::to_string(kAutosaveChunksPerTick) + "/tick (max " +
                                      std::to_string(kAutosaveMsPerTick) + "ms/tick).");
    }
}

void Server::tickAutosaveDrain() {
    if (worlds_.empty()) {
        autosave_draining_ = false;
        return;
    }

    auto t0 = std::chrono::steady_clock::now();
    // Distribute the chunk and time budgets across worlds with dirty chunks.
    int saved = 0;
    size_t chunk_budget = kAutosaveChunksPerTick;
    for (auto& w : worlds_) {
        if (!w || chunk_budget == 0)
            continue;
        if (w->unsavedChunkCount() == 0)
            continue;
        uint64_t elapsed = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::steady_clock::now() - t0)
                               .count();
        if (elapsed >= kAutosaveMsPerTick)
            break;
        int s = w->saveBudget(chunk_budget, kAutosaveMsPerTick - elapsed,
                              /*write_level_data=*/false);
        size_t used = (size_t)s < chunk_budget ? (size_t)s : chunk_budget;
        chunk_budget -= used;
        saved += s;
    }
    uint64_t tick_ms = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - t0)
                           .count();

    autosave_drain_written_ += (size_t)saved;
    ++autosave_drain_ticks_;
    if (tick_ms > autosave_drain_max_tick_ms_)
        autosave_drain_max_tick_ms_ = tick_ms;
    WorkbenchManager::get().onAutosave(tick_ms);
    if (tick_ms > 50) {
        utils::MainLogger::get().warn("[SAVE] Autosave flush took " + std::to_string(tick_ms) +
                                      "ms on one tick — this may cause "
                                      "client lag spikes!");
    }

    autosave_drain_idle_ticks_ = (saved == 0) ? (autosave_drain_idle_ticks_ + 1) : 0;
    size_t pending = 0;
    for (auto& w : worlds_) {
        if (w)
            pending += w->unsavedChunkCount();
    }

    // Manual-save progress: throttle to one line per 500 ms so a long
    // drain reports steadily without flooding the console.
    if (autosave_drain_manual_) {
        const uint64_t now_ms = nowSteadyMs();
        if (now_ms - autosave_drain_last_progress_ms_ >= 500) {
            autosave_drain_last_progress_ms_ = now_ms;
            notifySaveRequester("[SAVE] Progress: " +
                                std::to_string(autosave_drain_written_) + " chunk(s) written, " +
                                std::to_string(pending) + " pending, " +
                                std::to_string((now_ms - autosave_drain_start_ms_) / 1000) +
                                "s elapsed.");
        }
    }

    bool stuck = (pending > 0 && autosave_drain_idle_ticks_ >= kAutosaveIdleTicksMax);
    if (pending == 0 || stuck) {
        autosave_draining_ = false;
        uint64_t total_ms = nowSteadyMs() - autosave_drain_start_ms_;
        if (stuck) {
            // Manual saves do NOT quietly give up like the autosave does:
            // the requester asked for a full save point and gets told the
            // truth. The drain flag stays down; the next autosave (or
            // another /save) retries.
            const std::string why =
                "[SAVE] Save drain stopped with " + std::to_string(pending) +
                " chunk(s) still dirty (provider not accepting writes?) — " +
                "nothing was lost: the chunks stay marked modified and the next "
                "autosave or /save retries them.";
            if (autosave_drain_manual_)
                notifySaveRequester(why);
            else
                utils::MainLogger::get().warn(why);
        } else if (autosave_drain_written_ > 0 || autosave_drain_manual_) {
            if (autosave_drain_manual_) {
                // The manual save's full-save moment: level.dat now carries
                // the final time/spawn, every modified chunk is in the
                // providers' write queues or already durable in their
                // write-ahead logs. Report real completion, not intent.
                for (auto& w : worlds_) {
                    if (w)
                        w->saveBudget(/*max_chunks=*/0, /*max_ms=*/0,
                                      /*write_level_data=*/true);
                }
                notifySaveRequester("[SAVE] World saved: " +
                                    std::to_string(autosave_drain_written_) +
                                    " chunk(s) in " + std::to_string(total_ms) +
                                    "ms (" + std::to_string(autosave_drain_ticks_) +
                                    " ticks, worst tick " +
                                    std::to_string(autosave_drain_max_tick_ms_) +
                                    "ms). All modified chunks are persisted.",
                                    "commands.save.success");
            } else {
                utils::MainLogger::get().info(
                    "[SAVE] Autosave done: " + std::to_string(autosave_drain_written_) +
                    " chunk(s) en " + std::to_string(total_ms) + "ms (" +
                    std::to_string(autosave_drain_ticks_) + " tick(s), max " +
                    std::to_string(autosave_drain_max_tick_ms_) + "ms en un tick).");
            }
        }
        autosave_drain_manual_ = false;
        autosave_drain_requester_.clear();
    }
}

permission::BanList& Server::getNameBans() {
    if (!name_bans_) {
        name_bans_ = std::make_unique<permission::BanList>(dataPath_ + "/banned-players.json");
        name_bans_->load();
    }
    return *name_bans_;
}

permission::BanList& Server::getIPBans() {
    if (!ip_bans_) {
        ip_bans_ = std::make_unique<permission::BanList>(dataPath_ + "/banned-ips.json");
        ip_bans_->load();
    }
    return *ip_bans_;
}

// Operator list stored in <dataPath>/ops.txt.
namespace {
// Normalize operator names for consistent comparisons.
std::string normalizeOpName(std::string s) {
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s) {
        if (c == 0)
            continue; // Remove NUL bytes.
        if (c >= 0x80)
            continue; // Remove non-ASCII bytes.
        if (c < 0x20 || c == 0x7F)
            continue; // Remove control characters.
        out.push_back((char)c);
    }
    size_t b = 0, e = out.size();
    while (b < e && (out[b] == ' ' || out[b] == '\t'))
        ++b;
    while (e > b && (out[e - 1] == ' ' || out[e - 1] == '\t'))
        --e;
    out = out.substr(b, e - b);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return (char)std::tolower(c);
    });
    return out;
}
// Check whether a name matches the player login format.
bool looksLikeValidOpName(const std::string& s) {
    if (s.size() < 3 || s.size() > 16)
        return false;
    for (char c : s) {
        bool ok =
            (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
        if (!ok)
            return false;
    }
    return true;
}
} // namespace
void Server::loadOps() {
    ops_.clear();
    // Read bytes directly to handle UTF-8 and UTF-16 BOMs.
    const std::string ops_path = dataPath_ + "/ops.txt";
    std::ifstream in(ops_path, std::ios::binary);
    if (!in.is_open()) {
        // Log missing or inaccessible operator files instead of failing silently.
        const int e = errno;
        const std::string why = (e == ENOENT || e == 0)
                                    ? std::string("file not found")
                                    : std::string("cannot open (") + std::strerror(e) + ")";
        utils::MainLogger::get().debug(
            "No operators loaded from " + ops_path + ": " + why +
            ". Nobody is op until you /op <player> (written back to that path). ");
        return;
    }
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    // Strip UTF-8 BOMs commonly added by editors.
    auto utf16_to_8 = [](const std::string& raw, bool big_endian) {
        std::string s;
        s.reserve(raw.size() / 2);
        for (size_t i = 0; i + 1 < raw.size(); i += 2) {
            // Decode UTF-16 byte pairs according to endianness.
            unsigned lo = (unsigned char)raw[big_endian ? i + 1 : i];
            unsigned hi = (unsigned char)raw[big_endian ? i : i + 1];
            if (hi == 0 && lo != 0)
                s.push_back((char)lo);
        }
        return s;
    };
    if (content.size() >= 2 && (unsigned char)content[0] == 0xFF &&
        (unsigned char)content[1] == 0xFE) {
        content = utf16_to_8(content.substr(2), false);
    } else if (content.size() >= 2 && (unsigned char)content[0] == 0xFE &&
               (unsigned char)content[1] == 0xFF) {
        content = utf16_to_8(content.substr(2), true);
    } else if (content.size() >= 4 && content[0] != '\0' && content[1] == '\0' &&
               content[3] == '\0') {
        content = utf16_to_8(content, false);
    }
    // Remove a UTF-8 BOM if present.
    if (content.size() >= 3 && (unsigned char)content[0] == 0xEF &&
        (unsigned char)content[1] == 0xBB && (unsigned char)content[2] == 0xBF) {
        content.erase(0, 3);
    }

    // Normalize whitespace and names on every line.
    size_t pos = 0;
    while (pos <= content.size()) {
        size_t nl = content.find('\n', pos);
        std::string raw =
            (nl == std::string::npos) ? content.substr(pos) : content.substr(pos, nl - pos);
        pos = (nl == std::string::npos) ? content.size() + 1 : nl + 1;
        std::string line = normalizeOpName(raw);
        if (line.empty())
            continue; // Skip blank or invalid-only lines.
        if (!looksLikeValidOpName(line)) {
            // Keep invalid entries but warn that they cannot match login names.
            utils::MainLogger::get().warn("ops.txt entry cannot match any "
                                          "player name (kept anyway): '" +
                                          line + "' in " + ops_path);
        }
        ops_.insert(std::move(line));
    }

    // Operator names are private; the count rides DEBUG without the list
    // (the names stay in ops.txt). Invalid-entry warnings still print above.
    utils::MainLogger::get().debug("Loaded " + std::to_string(ops_.size()) + " operator(s) from " +
                                   ops_path);
}

void Server::saveOps() {
    const std::string ops_path = dataPath_ + "/ops.txt";
    std::ofstream out(ops_path, std::ios::trunc);
    if (!out.is_open()) {
        utils::MainLogger::get().warn("Failed to write " + ops_path + " (" + std::strerror(errno) +
                                      ") — operator changes stay in memory "
                                      "ONLY and will be lost on restart.");
        return;
    }
    for (const auto& name : ops_)
        out << name << "\n";
    out.flush();
    if (!out.good()) {
        utils::MainLogger::get().warn(
            "Failed to write " + ops_path +
            " — operator changes stay in memory ONLY and will be lost on restart.");
        return;
    }
    out.close();
}

bool Server::isOp(const std::string& name) const {
    // Normalize the queried name before comparing it with stored names.
    const std::string key = normalizeOpName(name);
    if (key.empty())
        return false;
    return ops_.find(key) != ops_.end();
}

void Server::setOp(const std::string& name, bool op) {
    const std::string key = normalizeOpName(name);
    if (key.empty())
        return;
    bool changed = false;
    if (op) {
        changed = ops_.insert(key).second;
    } else {
        changed = ops_.erase(key) > 0;
    }
    if (changed)
        saveOps();
    // Log the operator change and the file location.
    utils::MainLogger::get().info(std::string("/") + (op ? "op " : "deop ") + key +
                                  (op ? " -> operator" : " -> not operator") + " (" +
                                  std::to_string(ops_.size()) + " in " + dataPath_ + "/ops.txt)");
}

// Ensure the player data directory exists.
static void ensurePlayersDir(const std::string& dataPath) {
    std::string dir = dataPath + "/players";
    // Create the directory if needed.
    ::mkdir(dir.c_str(), 0755);
}

// Build the default NBT compound for a new player.
static NbtPtr buildDefaultPlayerNbt(Server* srv, const std::string& name) {
    cursestone::nbt::Tag* root = new cursestone::nbt::CompoundTag("");
    cursestone::nbt::set_long(root, "firstPlayed", (int64_t)(mcp_utils_time_ms()));
    cursestone::nbt::set_long(root, "lastPlayed", (int64_t)(mcp_utils_time_ms()));
    cursestone::nbt::set_short(root, "Health", 20);
    cursestone::nbt::set_short(root, "MaxHealth", 20);
    cursestone::nbt::set_short(root, "Hunger", 20);
    cursestone::nbt::set_short(root, "Air", 300);
    cursestone::nbt::set_int(root, "playerGameType", srv->gamemode());
    cursestone::nbt::set_string(root, "NameTag", name.c_str());
    cursestone::nbt::set_string(root, "Level", "world");
    if (srv->getDefaultWorld()) {
        auto sp = srv->getDefaultWorld()->spawn();
        cursestone::nbt::set_int(root, "SpawnX", (int32_t)sp.x);
        cursestone::nbt::set_int(root, "SpawnY", (int32_t)sp.y);
        cursestone::nbt::set_int(root, "SpawnZ", (int32_t)sp.z);
    }
    return adoptNbt(root);
}

NbtPtr Server::getOfflinePlayerData(const std::string& name) {
    ensurePlayersDir(dataPath_);
    std::string path = dataPath_ + "/players/" + name + ".dat";
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open())
        return buildDefaultPlayerNbt(this, name);

    std::vector<uint8_t> buf((std::istreambuf_iterator<char>(in)),
                             std::istreambuf_iterator<char>());
    if (buf.empty())
        return buildDefaultPlayerNbt(this, name);

    // Read reference-compatible GZIP-compressed big-endian NBT first.
    // Fall back to uncompressed formats used by legacy CurseStone files.
    cursestone::nbt::Tag* root = nullptr;
    {
        // Try GZIP-compressed data first.
        z_stream zs{};
        if (inflateInit2(&zs, 15 | 16) == Z_OK) {
            zs.next_in = (Bytef*)buf.data();
            zs.avail_in = (uInt)buf.size();
            std::vector<uint8_t> inflated;
            inflated.reserve(buf.size() * 4);
            uint8_t chunk[65536];
            // Ceiling for decompressed player data: a corrupt/hostile
            // players/<name>.dat must not expand unbounded.
            static const size_t kMaxInflated = 16u << 20; // 16 MiB
            int rc = Z_OK;
            do {
                zs.next_out = chunk;
                zs.avail_out = sizeof(chunk);
                rc = inflate(&zs, Z_NO_FLUSH);
                if (rc != Z_OK && rc != Z_STREAM_END)
                    break;
                size_t produced = sizeof(chunk) - zs.avail_out;
                if (produced > 0) {
                    if (inflated.size() + produced > kMaxInflated) {
                        rc = 0; // force the "not a gzip NBT" fallback below
                        break;
                    }
                    inflated.insert(inflated.end(), chunk, chunk + produced);
                }
            } while (rc != Z_STREAM_END && zs.avail_in > 0);
            inflateEnd(&zs);
            if (rc == Z_STREAM_END && !inflated.empty()) {
                root = cursestone::nbt::read(inflated.data(), inflated.size(), /*little_endian=*/false);
            }
        }
    }
    if (!root) {
        // Try uncompressed big-endian NBT.
        root = cursestone::nbt::read(buf.data(), buf.size(), /*little_endian=*/false);
    }
    if (!root) {
        // Try legacy uncompressed little-endian NBT.
        root = cursestone::nbt::read(buf.data(), buf.size(), /*little_endian=*/true);
    }
    if (!root) {
        utils::MainLogger::get().warn("Corrupt player data: " + path);
        return buildDefaultPlayerNbt(this, name);
    }
    return adoptNbt(root);
}

void Server::saveOfflinePlayerData(const std::string& name, NbtPtr nbt) {
    if (!nbt)
        return;
    ensurePlayersDir(dataPath_);
    std::string path = dataPath_ + "/players/" + name + ".dat";
    std::vector<uint8_t> bytes;
    cursestone::nbt::write(nbt.get(), /*little_endian=*/false, bytes);
    if (bytes.empty()) {
        utils::MainLogger::get().warn("Failed to serialize NBT for " + name);
        return;
    }
    // Compress player data using GZIP for the era reference compatibility.
    z_stream zs{};
    if (deflateInit2(&zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 15 | 16, 8, Z_DEFAULT_STRATEGY) !=
        Z_OK) {
        utils::MainLogger::get().warn("Failed to gzip player data for " + name);
        return;
    }
    size_t bound = deflateBound(&zs, (uLong)bytes.size());
    std::vector<uint8_t> gz(bound);
    zs.next_in = (Bytef*)bytes.data();
    zs.avail_in = (uInt)bytes.size();
    zs.next_out = (Bytef*)gz.data();
    zs.avail_out = (uInt)gz.size();
    int rc = deflate(&zs, Z_FINISH);
    deflateEnd(&zs);
    if (rc != Z_STREAM_END) {
        utils::MainLogger::get().warn("Failed to compress player data for " + name);
        return;
    }
    gz.resize(bound - zs.avail_out);

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        utils::MainLogger::get().warn("Failed to write player file: " + path);
        return;
    }
    out.write(reinterpret_cast<const char*>(gz.data()), (std::streamsize)gz.size());
    out.close();
}

// reference-compatible method implementations.
Player* Server::getPlayerExact(const std::string& name) const {
    const auto& snap = playersSnapshotRef();
    for (const auto& p : snap) {
        if (p) {
            std::string pn = p->getName();
            std::transform(pn.begin(), pn.end(), pn.begin(), [](unsigned char c) {
                return (char)::tolower(c);
            });
            std::string nl = name;
            std::transform(nl.begin(), nl.end(), nl.begin(), [](unsigned char c) {
                return (char)::tolower(c);
            });
            if (pn == nl)
                return p.get();
        }
    }
    return nullptr;
}

std::vector<Player*> Server::matchPlayer(const std::string& partialName) const {
    std::vector<Player*> result;
    std::string lower = partialName;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
        return (char)::tolower(c);
    });
    for (const auto& p : playersSnapshotRef()) {
        if (!p)
            continue;
        std::string pn = p->getName();
        std::transform(pn.begin(), pn.end(), pn.begin(), [](unsigned char c) {
            return (char)::tolower(c);
        });
        if (pn.find(lower) != std::string::npos) {
            result.push_back(p.get());
        }
    }
    return result;
}

// reference-compatible world management.
World* Server::getWorld(int worldId) const {
    if (worldId >= 1 && worldId <= (int)worlds_.size()) {
        return worlds_[worldId - 1].get();
    }
    return nullptr;
}

World* Server::getWorldByName(const std::string& name) const {
    for (auto& w : worlds_) {
        if (w && w->name() == name)
            return w.get();
    }
    return nullptr;
}

void Server::onPlayerLogin(Player* /*player*/) {
    // The era reference callback for successful player login.
    // Login handling and broadcasts are implemented in player::handleLogin.
}

std::string Server::getConfigString(const std::string& key, const std::string& def) const {
    // Stub for a future server.properties accessor.
    (void)key;
    return def;
}

int Server::getConfigInt(const std::string& key, int def) const {
    (void)key;
    return def;
}

bool Server::getConfigBool(const std::string& key, bool def) const {
    (void)key;
    return def;
}

void Server::setPropertyString(const std::string& key, const std::string& val) {
    (void)key;
    (void)val;
}

void Server::setPropertyInt(const std::string& key, int val) {
    (void)key;
    (void)val;
}

void Server::setPropertyBoolean(const std::string& key, bool val) {
    (void)key;
    (void)val;
}

int Server::broadcast(const std::string& message, const std::string& /*permissions*/) {
    broadcastMessage(message);
    return 0;
}

void Server::saveOfflinePlayerData(const std::string& name, NbtPtr nbt, bool /*async*/) {
    saveOfflinePlayerData(name, nbt);
}

void Server::titleTick() {
    // Update the MOTD with player count and TPS.
    if (network_) {
        std::string motd = "MCPE;" + motd_ + ";" + std::to_string(PROTOCOL_VERSION) + ";" +
                           std::string(MINECRAFT_VERSION) + ";" +
                           std::to_string(playersSnapshotRef().size()) + ";" +
                           std::to_string(max_players_);
        // Access setServerName through the RakLib interface.
        auto* raklib = dynamic_cast<network::RakLibInterface*>(network_->getRakLib());
        if (raklib)
            raklib->setServerName(motd);
    }
}

void Server::sendRecipeList(Player* player) {
    // Send all loaded crafting recipes to the player.
    if (!player || !crafting_)
        return;
    auto cd = std::make_shared<network::protocol::CraftingDataPacket>();
    cd->addAllRecipes(*crafting_);
    player->sendPacket(cd);
}

void Server::updatePlayerListData(const std::string& /*uuid*/, const std::string& /*name*/,
                                  int64_t /*eid*/, const std::string& /*skin*/) {
    // Player list additions are handled by player::doFirstSpawn.
}

void Server::removePlayerListData(const std::string& /*uuid*/) {
    // Player list removals are handled by player::close.
}

void Server::sendFullPlayerListData(Player* /*target*/) {
    // Initial player list synchronization is handled by player::doFirstSpawn.
}

} // namespace cursestone