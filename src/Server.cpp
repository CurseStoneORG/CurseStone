/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

 * CurseStone | Open Source server Software
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
// server singleton implementation (Phase 21 completion).
#include "Server.h"
#include "MemoryManager.h"
#include "workbench/WorkbenchManager.h"
#include "diagnostics/Diagnostics.h"
#include "Player.h"
#include "utils/MainLogger.h"
#include "utils/MainLogger.hpp"
#include "utils/Config.h"
#include "utils/Utils.h"
#include "utils/TextColor.h"

#include <climits>
#include <cstring>
#include <cerrno>
#include <thread>
#include <chrono>
#include <cstdlib> // std::_Exit
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
#include "scheduler/AsyncPool.h"
#include "scheduler/ServerScheduler.h"
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
#include "plugin/PluginManager.h" // plugins Lua (fase 3)
#include "utils/Config.h"
#include "utils/DefaultConfig.h"

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

// Reads to boolean accepting true/on/yes/and/1 (case-insensitive).
bool readBool(const utils::Config& cfg, const char* key, bool def) {
    std::string v = cfg.getString(key, std::string());
    if (v.empty())
        return def;
    std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) {
        return std::tolower(c);
    });
    return v == "true" || v == "on" || v == "yes" || v == "y" || v == "1";
}

} // namespace

void Server::loadConfig() {
    using utils::Config;
    Config props; // server.properties: basic settings
    Config adv;   // cursestone.yml: advanced settings

    const std::string props_path = dataPath_ + "/server.properties";
    const std::string yaml_path = dataPath_ + "/cursestone.yml";
    if (props.loadIni(props_path)) {
        utils::MainLogger::get().info("Loaded config: " + props_path);
    } else {
        utils::MainLogger::get().warn("Missing " + props_path + "; using defaults");
    }
    if (adv.loadYaml(yaml_path)) {
        utils::MainLogger::get().info("Loaded config: " + yaml_path);
    } else {
        utils::MainLogger::get().warn("Missing " + yaml_path + "; using defaults");
    }

    // server.properties
    ip_ = props.getString("server-ip", "0.0.0.0");
    port_ = (uint16_t)props.getInt("server-port", 19132);
    motd_ = props.getString("motd", "CurseStone");
    max_players_ = (int)props.getInt("max-players", 20);
    gamemode_ = (int)props.getInt("gamemode", 0);
    difficulty_ = (int)props.getInt("difficulty", 1);
    whitelist_ = readBool(props, "white-list", false);
    online_mode_ = readBool(props, "online-mode", false);
    view_distance_ = (int)props.getInt("view-distance", 8);

    // cursestone.yml
    async_workers_ = (int)adv.getInt("settings.async-workers", 4);
    autosave_ticks_ = (int)adv.getInt("settings.autosave-ticks", 6000);
    advanced_log_ = readBool(adv, "settings.advanced-log", true);

    // Both values must be set before the tick loop so the first GC pass
    // already honors the configured budget.
    MemoryManager::get().setLimitMB((int)adv.getInt("memory.max-mb", 512));
    MemoryManager::get().setGcIntervalTicks(
        (uint64_t)std::max<int64_t>(20, adv.getInt("memory.gc-interval-ticks", 200)));

    // Plugin limits: 0 disables a limit. Root-level keys are accepted as a fallback.
    plugin::PluginLimits pluginLimits;
    pluginLimits.instructions = adv.getInt("settings.plugin-instruction-limit",
                                           adv.getInt("plugin-instruction-limit", 1000000));
    pluginLimits.timeMs =
        adv.getInt("settings.plugin-time-limit-ms", adv.getInt("plugin-time-limit-ms", 100));
    pluginLimits.maxErrors =
        (int)adv.getInt("settings.plugin-max-errors", adv.getInt("plugin-max-errors", 10));
    plugin::LuaPlugin::setLimits(pluginLimits);

    utils::MainLogger::get().info("Server port: " + std::to_string(port_));
    utils::MainLogger::get().info("Server IP: " + ip_);
    utils::MainLogger::get().info("MOTD: " + motd_);
    utils::MainLogger::get().info("Max players: " + std::to_string(max_players_));
    utils::MainLogger::get().info("Gamemode: " + std::to_string(gamemode_) +
                                  " (0=survival, 1=creative, 2=adventure, 3=spectator)");
    utils::MainLogger::get().info("World generator: " + adv.getString("world.generator", "normal"));
    utils::MainLogger::get().info("World format: " + adv.getString("world.format", "anvil"));
    utils::MainLogger::get().info(
        "Plugin limits: instructions=" + std::to_string(plugin::LuaPlugin::limits().instructions) +
        ", time-ms=" + std::to_string(plugin::LuaPlugin::limits().timeMs) +
        ", max-errors=" + std::to_string(plugin::LuaPlugin::limits().maxErrors) + " (0 = off)");
}

void Server::initLogger() {
    // Open server.log in the data directory so all log lines are written
    // to disk in actual time (PMMP-equivalent: pocketmine.log).
    std::string log_path = dataPath_ + "/server.log";
    mcp_main_logger_init(log_path.c_str(), /*colored=*/true);
    utils::MainLogger::get().info(std::string(CURSESTONE_NAME) + " v" + CURSESTONE_API +
                                  " (codename: " + CURSESTONE_CODENAME + ")");
    utils::MainLogger::get().info(" Minecraft PE " + std::string(MINECRAFT_VERSION) +
                                  ", protocol " + std::to_string(PROTOCOL_VERSION));
    utils::MainLogger::get().info("Log file: " + log_path);
}

void Server::registerBlocksAndItems() {
    block::register_all_blocks();
    item::Item::registerAll();
    utils::MainLogger::get().info("Registered IDs");
}

void Server::openNetwork() {
    network_ = std::make_shared<network::Network>(this);
    auto rli = std::make_unique<network::RakLibInterface>(this);
    // Keep to raw pointer so we can call start() after the unique_ptr is moved
    // into the Network (which takes ownership). start() opens the UDP socket
    // and registers the MOTD; without it, the server binds nothing and clients
    // cannot connect.
    auto* rli_ptr = rli.get();
    network_->registerInterface(std::move(rli));
    rli_ptr->start();
    utils::MainLogger::get().info("Network bound to " + ip_ + ":" + std::to_string(port_));
}

void Server::loadDefaultWorld() {
    // World name and seed come from server.properties; format and generator
    // from cursestone.yml.
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
    lc.dataPath = dataPath_; // so World can compute absolute world paths

    // Create the world directory structure BEFORE constructing the World,
    // so the WorldProvider can open the region/ subdirectory on init().
    // Mirrors Nukkit's Anvil.generate(): worlds/<name>/ + worlds/<name>/region/
    std::string worldsDir = dataPath_ + "/worlds";
    std::string worldDir = worldsDir + "/" + lc.name;
    std::string regionDir = worldDir + "/region";
    ::mkdir(worldsDir.c_str(), 0755);
    ::mkdir(worldDir.c_str(), 0755);
    ::mkdir(regionDir.c_str(), 0755);

    // Construct the World — this instantiates the WorldProvider (Anvil by
    // default) and calls provider->init(worldDir). The provider creates the
    // region/ subdir if missing and reads level.dat if it exists.
    world_ = std::make_shared<world::World>(std::move(lc));
    worlds_.push_back(world_);

    utils::MainLogger::get().info("World directory: " + worldDir);
    utils::MainLogger::get().info("Region directory: " + regionDir);
}

// ---------------------------------------------------------------------
// Multiworld: load and unloads of worlds in time of execution.
// ---------------------------------------------------------------------
// the world by default vive in world_ (and also in worlds_[0]); the
// worlds extra that crean the plugins is add to worlds_ and is
// tick/are stored in the same fases that the main.

World* Server::loadWorld(const std::string& name, const std::string& generator) {
    if (name.empty())
        return nullptr;
    // already loaded: return the existente without touch nothing.
    if (World* existing = getWorldByName(name))
        return existing;

    world::WorldConfig lc;
    lc.name = name;
    lc.format = "anvil";
    lc.generator = generator.empty() ? "normal" : generator;
    lc.seed = (int64_t)mcp_utils_time_ms();
    lc.view_distance = view_distance_;
    lc.dataPath = dataPath_;

    // same estructura of folders that loadDefaultWorld(): the provider
    // needs worlds/<name>/region/ before init().
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
    utils::MainLogger::get().info("World '" + name +
                                  "' loaded (generator=" + lv->config().generator +
                                  ", dimension=" + std::to_string(lv->dimension()) + ")");
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

        // moves to the players of this world in the spawn of the world
        // by default before soltar the shared_ptr.
        for (auto& p : playersSnapshot()) {
            if (p && p->getWorld() == w) {
                auto sp = world_ ? world_->spawn() : mcp_v3(0, 64, 0);
                p->switchWorld(world_.get(), sp.x, sp.y, sp.z, false);
            }
        }

        w->save();
        worlds_.erase(it);
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
        if (w && w->name() == name)
            return w;
    }
    return nullptr;
}

void Server::startCommandReader() {
    console_reader_ = std::make_unique<CommandReader>([this](const std::string& line) {
        if (line.empty())
            return;
        ConsoleCommandSender sender;
        if (command_map_)
            command_map_->dispatch(&sender, line);
    });
    console_reader_->start();
}

void Server::start(const std::string& dataPath) {
    // Record the boot start time so we can log "server started in N.Ns"
    // at the end. Mirrors PMMP's startup timing log.
    auto boot_start = std::chrono::steady_clock::now();

    dataPath_ = dataPath;
    running_.store(true, std::memory_order_relaxed);

    // Diagnostics: mark the server start so crashdumps can report uptime.
    diagnostics::Diagnostics::get().setUptimeStart(
        (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());

    // Ensure the data dir exists.
    std::error_code ec;
    std::filesystem::create_directories(dataPath_, ec);

    // Initialize logger first so we can see config-loading messages.
    initLogger();

    // Seed any missing config file from the built-in defaults.
    std::vector<std::string> created;
    if (!utils::writeDefaultConfigs(dataPath_, &created)) {
        utils::MainLogger::get().warn("Could not create default config files in " + dataPath_);
    }
    for (const auto& name : created) {
        utils::MainLogger::get().info("Created default " + name);
    }

    utils::MainLogger::get().info("Loading configuration...");
    loadConfig();
    loadOps();
    utils::MainLogger::get().info("Registering blocks and items...");
    registerBlocksAndItems();
    command_map_ = std::make_shared<SimpleCommandMap>();
    registerDefaultCommands(*command_map_);
    crafting_ = std::make_shared<inventory::CraftingManager>();
    // Recipes live IN CODE (CraftingManager::registerBuiltinRecipes):
    // there is not recipe file to load. An older build read an external
    // file that was not packaged with the binary, so to clean release
    // ended up with not recipes at all.
    int recipeCount = crafting_->registerBuiltinRecipes();
    utils::MainLogger::get().info("Loaded " + std::to_string(recipeCount) + " crafting recipes");
    async_pool_ = std::make_shared<scheduler::AsyncPool>(async_workers_);
    utils::MainLogger::get().info("Opening network interface...");
    openNetwork();
    utils::MainLogger::get().info("Loading default world...");
    loadDefaultWorld();
    // Lua plugins: loaded between the world and the console reader. The
    // world already exists (plugins can touch it) and the console does
    // not receive commands yet (not command arrives while plugins
    // register).
    utils::MainLogger::get().info("Loading plugins...");
    plugin::PluginManager::get().loadAll(dataPath_ + "/plugins", dataPath_ + "/data_pl");
    utils::MainLogger::get().info("Starting console command reader...");
    startCommandReader();

    // Compute total boot time.
    auto boot_end = std::chrono::steady_clock::now();
    auto boot_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(boot_end - boot_start).count();
    double boot_sec = (double)boot_ms / 1000.0;
    utils::MainLogger::get().info("Server ready Tick loop starting...");
    utils::MainLogger::get().info("Server started in " + std::to_string(boot_sec) + "s");

    // Give the OS to brief moment to flush filesystem buffers after the
    // world directory + level.dat + provider init writes. Without this,
    // the tick loop can race with filesystem sync on slow devices
    // (Android especially). 200ms is plenty for any reasonable storage.
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
}

void Server::runTickLoop() {
    uint64_t last_tick_count = 0;
    auto last_tps_time = std::chrono::steady_clock::now();
    //
    // tick pacing uses an ABSOLUTE calendar (sleep_until) so drift
    // cannot accumulate. The old relative sleep (50 - trunc(elapsed_ms))
    // dropped the fraction of every tick (30.9ms elapsed slept 20ms ->
    // to 50.9ms tick) and added 1-3ms of Linux scheduler overshoot on
    // top: the error compounded until the server fell below 20 TPS.
    // Below 20 TPS the server's item simulation runs slower than the
    // client's (which integrates at its own 20fps): to dropped item rests
    // in one place on the server and another on the client, particle
    // packets go out late, and everything visibly drags behind.
    //
    // next_tick advances by exactly 50ms every tick - overshoot is paid
    // back on the next tick instead of accumulating, exactly like
    // PMMP/Genisys do with their checkTick.
    auto next_tick = std::chrono::steady_clock::now();
    while (running_.load(std::memory_order_relaxed)) {
        // Poll the external stop flag (set by CurseStone.cpp's SIGINT/SIGTERM
        // handler) on the main thread, in normal — not signal — context.
        // This is the only place that flag is ever acted on; see
        // setExternalStopFlag()'s comment for why the handler itself
        // can't call shutdown() directly.
        if (external_stop_flag_ && *external_stop_flag_ &&
            running_.load(std::memory_order_relaxed)) {
            shutdown();
            break;
        }
        auto start_ts = std::chrono::steady_clock::now();
        tick();
        auto elapsed = std::chrono::steady_clock::now() - start_ts;
        auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
        next_tick += std::chrono::milliseconds(CURSESTONE_TICK_MS);
        if (std::chrono::steady_clock::now() < next_tick) {
            std::this_thread::sleep_until(next_tick);
        } else {
            // Overrun tick (slow frame): re-anchor the calendar to the
            // present so to run of late ticks does not chain (rage
            // catch-up) - to PMMP server does the same.
            next_tick = std::chrono::steady_clock::now();
        }
        (void)elapsed_ms;
        // Compute TPS once per second.
        if (currentTick_ - last_tick_count >= 20) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed_sec =
                std::chrono::duration_cast<std::chrono::milliseconds>(now - last_tps_time).count();
            if (elapsed_sec > 0)
                tps_ = (int)(20000 / elapsed_sec);
            last_tps_time = now;
            last_tick_count = currentTick_;
        }
    }
    utils::MainLogger::get().info("Tick loop exited.");
    tick_loop_exited_.store(true, std::memory_order_relaxed);
    // Lua plugins: the tick loop has exited (nothing fires events)
    // and the world can still be saved - disable all plugins here.
    plugin::PluginManager::get().disableAll();

    // Cleanup — runs on the main thread, AFTER the tick loop exits.
    // This is critical: /stop is dispatched from the CommandReader
    // thread, so doing saveAll() inside server::shutdown() would race
    // with the main thread destroying server members (mutexes, world_,
    // players_). By deferring all cleanup to here, we guarantee that
    // saveAll() runs single-threaded before any destructor fires.
    if (network_)
        network_->tick(); // graceful RakLib close
    // end global GC pass BEFORE saveAll(): flushes tile/entities, saves
    // out-of-view chunks as part of the chunk sweep, and drops picked-up
    // item records so nothing stale is written back to disk. Forced —
    // rate limiting must not eat the last pass.
    MemoryManager::get().collect(/*force=*/true);
    saveAll();
    if (console_reader_)
        console_reader_->stop();

    // Stop and JOIN the chunk-generation pool here, on the main thread,
    // while the process is still fully alive. The pool is owned by this
    // server singleton, and the order in which static destructors run is
    // not guaranteed: if to worker is still inside GenerationTask::run()
    // when the global block registry / chunk tables are destroyed, it dies
    // in mcp_block_light_opacity() reading to table that not longer exists —
    // to SIGSEGV logged right AFTER "Shutdown complete." (reproducible
    // whenever to player had chunks in flight at stop time). stop() joins
    // every worker and is idempotent, so the pool's own destructor stays to
    // not-op and nothing can be generating once we return.
    if (async_pool_)
        async_pool_->stop();

    utils::MainLogger::get().info("Shutdown complete.");
}

void Server::tick() {
    ++currentTick_;

    // Diagnostics: keep the current tick visible to crashdumps.
    diagnostics::Diagnostics::get().setTick(currentTick_);

    // --- Workbench: tick start hook ---
    auto wb_start = std::chrono::steady_clock::now();
    WorkbenchManager::get().onTickStart();

    // --- Reap players flagged for removal by player::close() ---
    {
        std::lock_guard<std::mutex> lk(players_mtx_);
        players_.erase(std::remove_if(players_.begin(), players_.end(),
                                      [](const std::shared_ptr<Player>& p) {
                                          return !p || p->isPendingRemoval();
                                      }),
                       players_.end());
    }

    // --- Phase: scheduler ---
    // FIX: previously this only called tick() when there were pending
    // SYNC handlers (size() > 0). But ServerScheduler::tick() also
    // drains the ASYNC completion queue (onCompletion() callbacks for
    // finished AsyncTasks) — with the old guard, if every sync task
    // had finished but an AsyncTask (e.g. to chunk save) completed on
    // to worker thread, its onCompletion() would never run because
    // tick() itself was being skipped. Now we always call tick(); the
    // cost when there's genuinely nothing to do is two cheap
    // near-empty lock/unlock pairs, not worth special-casing away.
    auto t0 = std::chrono::steady_clock::now();
    scheduler::ServerScheduler::get().tick(currentTick_);
    auto t1 = std::chrono::steady_clock::now();
    WorkbenchManager::get().onTickPhase(
        "scheduler",
        (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count());

    // --- Phase: world ---
    t0 = std::chrono::steady_clock::now();
    for (auto& w : worlds_) {
        if (w)
            w->tick(currentTick_);
    }
    t1 = std::chrono::steady_clock::now();
    WorkbenchManager::get().onTickPhase(
        "level", (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count());

    // --- Phase: block entities (furnaces) ---
    // Runs outside World::tick to keep that phase free of packet
    // sends (furnaces send progress/content to viewers here).
    t0 = std::chrono::steady_clock::now();
    for (auto& w : worlds_) {
        if (w)
            w->tickTiles(currentTick_);
    }
    t1 = std::chrono::steady_clock::now();
    WorkbenchManager::get().onTickPhase(
        "tiles", (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count());

    // --- Phase: global GC ---
    // THE single GarbageCollector pass for the whole server. Everything
    // that reclaims shared world memory funnels through here:
    // chunk unload sweep (was to separate 100-tick schedule in this very
    // function) + dropped-item despawn/taken sweep (was to per-tick sweep
    // inside World::tick) + heap trim. Centralizing matters: independent
    // reclaimer schedules were freeing world state (dropped items, tiles,
    // chunks) in the middle of player actions — the "player actions
    // desyncing from the server" symptom. Rate limiting inside
    // MemoryManager::collect() keeps the pass off every tick; it prices
    // like the old chunk sweep (occasional maintenance, may hit disk).
    t0 = std::chrono::steady_clock::now();
    MemoryManager::get().collect();
    t1 = std::chrono::steady_clock::now();
    WorkbenchManager::get().onTickPhase(
        "gc", (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count());

    // --- Phase: network (first pass) ---
    // CRITICAL: process network BEFORE player updates.
    t0 = std::chrono::steady_clock::now();
    if (network_)
        network_->tick();
    t1 = std::chrono::steady_clock::now();
    WorkbenchManager::get().onTickPhase(
        "network",
        (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count());

    // Update watchdog: main thread is alive
    WorkbenchManager::get().last_drain_time_ms.store(
        (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count(),
        std::memory_order_relaxed);

    // --- Phase: player updates ---
    auto snap = playersSnapshot();
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

    // Diagnostics: refresh the online-player snapshot (twice per second).
    // It is captured here, on the main thread, so the SIGSEGV handler can
    // read to ready-made snapshot instead of touching server state while
    // the process is dying.
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

    // --- Phase: batched packet flush ---
    // Flushes each player's per-tick buffer as ONE compressed
    // BatchPacket per channel. Everything this tick produced - the
    // network handlers, the world tick, and the player updates - goes
    // out here, grouped (see the note in player::sendPacket).
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

    // --- Phase: network (second pass — flush outbound) ---
    if (network_)
        network_->tick();

    // --- Phase: auto-save (incremental) ---
    // Previously this block called world_->save(), which serializes ALL
    // dirty chunks in the same tick. With the world running that is
    // hundreds of chunks (everything generated since the last autosave),
    // i.e. several hundred ms of blocked main thread: measured ~495 ms
    // for 350 chunks and ~759 ms in the reported case, ~90% of it zlib
    // compression of the chunks (see kChunkCompressionLevel in Anvil).
    // to live client experiences that as to freeze/disconnect.
    // Now the autosave tick saves the cheap data (player data +
    // level.dat) and OPENS the drain; on the following ticks the queue
    // is written to few chunks per tick under to time budget, so the
    // per-tick cost stays bounded and the world still ends up fully
    // flushed to disk.
    if (autosave_ticks_ > 0 && currentTick_ % autosave_ticks_ == 0) {
        auto psnap = playersSnapshot();
        for (auto& p : psnap) {
            if (p)
                p->saveData(); // NBT of player: barato, not is I/or of chunks
        }
        beginAutosaveDrain();
    }
    if (autosave_draining_)
        tickAutosaveDrain();

    // --- Workbench: tick end hook ---
    auto wb_end = std::chrono::steady_clock::now();
    uint64_t total_ms =
        (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(wb_end - wb_start).count();
    WorkbenchManager::get().onTickEnd(total_ms);
}

void Server::shutdown() {
    // Signal the tick loop to exit. The current cleanup (saveAll,
    // console_reader stop) is deferred to AFTER the tick loop exits
    // in runTickLoop(), because /stop may be dispatched from the
    // CommandReader thread. Doing saveAll() here would race with the
    // main thread's destructors (mutexes, world_, players_) — causing
    // "pthread_mutex_lock called on to destroyed mutex" and lost chunks.
    //
    // EMERGENCY WATCHDOG: if the main tick loop is frozen (e.g. by to
    // runaway synchronous operation), running_ is set to false but the
    // tick loop never gets to check it. /stop then hangs forever with
    // "Stopping the server..." printed and not exit. We spawn to short
    // watcher thread that gives the main loop 5 seconds to exit
    // gracefully; if it doesn't, we _exit() the process so the user
    // at least gets to prompt back. Data loss is acceptable in this
    // scenario — the alternative is to permanently hung server.
    if (running_.exchange(false, std::memory_order_relaxed)) {
        utils::MainLogger::get().info("Server shutting down...");
        std::thread([this]() {
            // Give the main loop 5 seconds to exit cleanly.
            for (int i = 0; i < 50; ++i) {
                if (!running_.load(std::memory_order_relaxed) &&
                    tick_loop_exited_.load(std::memory_order_relaxed)) {
                    return; // main loop exited cleanly
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            // Timeout — force exit. We can't call ~server() safely here
            // because we're on to different thread and the destructors
            // may race, but _exit() bypasses destructors entirely.
            utils::MainLogger::get().error("Shutdown watchdog: tick loop did not exit within 5s — "
                                           "forcing process exit (unsaved chunks may be lost).");
            std::_Exit(1);
        }).detach();
    }
}

Player* Server::getPlayer(const std::string& name) const {
    auto snap = playersSnapshot();
    for (const auto& p : snap)
        if (p && p->getName() == name)
            return p.get();
    return nullptr;
}
void Server::addPlayer(std::shared_ptr<Player> p) {
    std::lock_guard<std::mutex> lk(players_mtx_);
    players_.push_back(std::move(p));
}
void Server::removePlayer(Player* p) {
    std::lock_guard<std::mutex> lk(players_mtx_);
    players_.erase(std::remove_if(players_.begin(), players_.end(),
                                  [p](const std::shared_ptr<Player>& q) {
                                      return q.get() == p;
                                  }),
                   players_.end());
}
std::vector<std::shared_ptr<Player>> Server::playersSnapshot() const {
    std::lock_guard<std::mutex> lk(players_mtx_);
    return players_;
}
void Server::broadcastMessage(const std::string& msg) {
    auto snap = playersSnapshot();
    for (auto& p : snap)
        if (p)
            p->sendMessage(msg);
    // Every broadcast (player chat, join/quit/death messages, server
    // say) is mirrored to the server console with the §-codes stripped -
    // exactly like PMMP's server::broadcastMessage logging behaviour, so
    // an admin watching the console sees the same chat the players see
    // in-game.
    utils::MainLogger::get().info("[CHAT] " + TC::clean(msg));
}
void Server::broadcastPacket(void* /*p*/) {
    // send to each player's source interface
}
int64_t Server::allocateEid() {
    return next_eid_.fetch_add(1, std::memory_order_relaxed);
}
void Server::saveAll() {
    // 1. Save every online player's NBT + inventory FIRST, so any
    //    chunk-modifications they triggered (block place/break) get
    //    marked dirty on the chunks before the world save runs.
    auto snap = playersSnapshot();
    for (auto& p : snap) {
        if (p)
            p->saveData();
    }
    utils::MainLogger::get().info("[SAVE] " + std::to_string(snap.size()) + " player(s) saved.");

    // 2. Save every loaded world — flushes dirty chunks to .mca files
    //    AND writes level.dat (seed/spawn/time) via the provider.
    for (auto& w : worlds_) {
        if (!w)
            continue;
        int saved = w->save();
        utils::MainLogger::get().info("[SAVE] World '" + w->name() + "' saved (" +
                                      std::to_string(saved) + " chunks flushed).");
    }
}

// Autosave incremental
//
// PocketMine-style pipeline: the drain on the MAIN thread only serializes
// dirty chunks (Anvil NBT encode, ~0.1 ms/chunk) and hands the snapshots
// to the provider's background writer, which does the ZLIB compression
// and the .mca region I/O off-thread. That moved the expensive half of
// the old synchronous path (compression + disk, the source of the 12 ms
// ticks users saw while exploring) out of the tick entirely.
//
// Budget: the encode-only cost lets us enqueue a much bigger batch per
// tick than before while keeping the main-thread cap at 3 ms. A 469-chunk
// backlog now drains in ~10-15 ticks with a worst-case main-thread tick
// of ~3 ms (was 8 chunks/tick with 12 ms spikes), and the disk work
// overlaps gameplay on the worker thread.
static constexpr size_t kAutosaveChunksPerTick = 64;
static constexpr uint64_t kAutosaveMsPerTick = 3;
// If the flush writes nothing for this many ticks in to row (e.g. to
// provider that always fails), the drain is closed instead of
// retrying the same dirty chunk forever.
static constexpr int kAutosaveIdleTicksMax = 40;

static uint64_t nowSteadyMs() {
    return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

void Server::beginAutosaveDrain() {
    autosave_draining_ = false;
    if (worlds_.empty())
        return;

    autosave_drain_start_ms_ = nowSteadyMs();
    autosave_drain_written_ = 0;
    autosave_drain_ticks_ = 0;
    autosave_drain_max_tick_ms_ = 0;
    autosave_drain_idle_ticks_ = 0;

    // level.dat (world time + spawn) is written HERE, once, with to
    // zero-chunk budget: it is to tiny file and we do not want to
    // rewrite it on every drain tick.
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

    size_t pending = 0;
    for (auto& w : worlds_) {
        if (w)
            pending += w->unsavedChunkCount();
    }
    if (pending == 0)
        return; // nothing sucio: ni abrimos the drenaje

    autosave_draining_ = true;
    if (advanced_log_) {
        utils::MainLogger::get().info("[SAVE] Autosave: " + std::to_string(pending) +
                                      " chunk(s) en cola -> drenando " +
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
    // the presupuesto is reparte between the worlds with chunks sucios:
    // each uno drena with the part that it quede of chunks and time.
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
    bool stuck = (pending > 0 && autosave_drain_idle_ticks_ >= kAutosaveIdleTicksMax);
    if (pending == 0 || stuck) {
        autosave_draining_ = false;
        uint64_t total_ms = nowSteadyMs() - autosave_drain_start_ms_;
        if (stuck) {
            utils::MainLogger::get().warn(
                "[SAVE] Autosave drain stopped with " + std::to_string(pending) +
                " chunk(s) still dirty (provider "
                "not accepting writes?) — will retry on the next autosave.");
        } else if (autosave_drain_written_ > 0) {
            utils::MainLogger::get().info(
                "[SAVE] Autosave done: " + std::to_string(autosave_drain_written_) +
                " chunk(s) en " + std::to_string(total_ms) + "ms (" +
                std::to_string(autosave_drain_ticks_) + " tick(s), max " +
                std::to_string(autosave_drain_max_tick_ms_) + "ms en un tick).");
        }
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

// Operator list — backed by <dataPath>/ops.txt (one name per line)
namespace {
// Normalizes to player name to the canonical comparison form:
//   - drops the 3 bytes of to UTF-8 BOM and ANY non-ASCII byte
//     (NBSP, zero-width, to misinterpreted UTF-16 sequence, ...),
//   - removes NULs and control characters (\r, \n, \t, of the),
//   - trims whitespace from both ends,
//   - lowercases the result.
//
// The SAME function is used to LOAD ops.txt and to LOOK UP from
// isOp()/setOp(). That way garbage from wherever it comes - an
// Android editor that leaves BOM/NBSP, to CRLF or UTF-16 file, or to
// name with one stray byte glued on - ends up in the same shape on
// both sides and matches. Previously only the data coming from the
// file was cleaned while the queried name was merely lowercased: if
// the query carried any invisible extra byte, isOp() failed to find
// it and the op silently kept not permissions.
std::string normalizeOpName(std::string s) {
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s) {
        if (c == 0)
            continue; // NUL (UTF-16 leido as text)
        if (c >= 0x80)
            continue; // BOM, NBSP, zero ancho, acentos
        if (c < 0x20 || c == 0x7F)
            continue; // \r \n \t and other control
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
// Same rule PlayerLogin.cpp accepts for joining: [to-zA-Z0-9_]{3,16}.
// to name outside that rule can NEVER match to connected player, so we
// warn (instead of staying silent) to make it visible in the log.
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
    // Binary read: to UTF-8 BOM would arrive in text mode too, but this
    // way the file is read byte by byte and we can strip it ourselves.
    const std::string ops_path = dataPath_ + "/ops.txt";
    std::ifstream in(ops_path, std::ios::binary);
    if (!in.is_open()) {
        // Explicit diagnostic: the symptom "I add myself to ops.txt / run
        // /op and the server does not make me an op" almost always means
        // THIS file is not being read (to different path on Android, not
        // permissions, or it does not exist yet). Previously the code
        // returned silently and the server started with zero ops.
        const int e = errno;
        const std::string why = (e == ENOENT || e == 0)
                                    ? std::string("file not found")
                                    : std::string("cannot open (") + std::strerror(e) + ")";
        utils::MainLogger::get().info(
            "No operators loaded from " + ops_path + ": " + why +
            ". Nobody is op until you /op <player> (written back to that path). ");
        return;
    }
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    // An editor that saves as "UTF-8 with BOM" (Notepad on Windows,
    // several Android editors) leaves the 3 bytes 0xEF 0xBB 0xBF at the
    // start. Without stripping them the FIRST stored entry is
    // "\xEF\xBB\xBFdr1xy" and it NEVER matches the player's name: the
    // file looks perfect, the log says "Loaded 1 operator(s)" and still
    // that op cannot use commands.
    // UTF-16: several Android editors store text as UTF-16 (BOM FF FE /
    // FE FF, or not BOM but to NUL in every odd byte). Without decoding,
    // "Dr1xy" reads as "D\0r\0..." and NEVER matches the player's name
    // even though the file looks perfect.
    auto utf16_to_8 = [](const std::string& raw, bool big_endian) {
        std::string s;
        s.reserve(raw.size() / 2);
        for (size_t i = 0; i + 1 < raw.size(); i += 2) {
            // it: even byte = low, odd byte = high. BE: the other way around.
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
    // BOM UTF-8 (Notepad in Windows, several editores of Android).
    if (content.size() >= 3 && (unsigned char)content[0] == 0xEF &&
        (unsigned char)content[1] == 0xBB && (unsigned char)content[2] == 0xBF) {
        content.erase(0, 3);
    }

    // Trims whitespace (including tabs) from BOTH ends of every line.
    // Previously only the tail was trimmed, so to line " Dr1xy" did not
    // match either.
    size_t pos = 0;
    size_t weird = 0;
    while (pos <= content.size()) {
        size_t nl = content.find('\n', pos);
        std::string raw =
            (nl == std::string::npos) ? content.substr(pos) : content.substr(pos, nl - pos);
        pos = (nl == std::string::npos) ? content.size() + 1 : nl + 1;
        std::string line = normalizeOpName(raw);
        if (line.empty())
            continue; // lineas in blanco / only basura
        if (!looksLikeValidOpName(line)) {
            ++weird;
            // Loaded anyway (to user entry is never dropped) but warned
            // about: to name PlayerLogin would reject can never match
            // anyone joining the server.
            utils::MainLogger::get().warn("ops.txt entry cannot match any "
                                          "player name (kept anyway): '" +
                                          line + "' in " + ops_path);
        }
        ops_.insert(std::move(line));
    }

    // The log lists the recognized names: this is how you tell "my name
    // is in ops.txt but the server did not read it" apart from "the
    // server does have me as op and the problem is elsewhere".
    std::string names;
    for (const auto& n : ops_) {
        if (!names.empty())
            names += ", ";
        names += n;
    }
    utils::MainLogger::get().info(
        "Loaded " + std::to_string(ops_.size()) + " operator(s) from " + ops_path +
        (names.empty() ? std::string() : ": " + names) +
        (weird ? " (" + std::to_string(weird) + " suspicious entry/entries, see warnings above)"
               : std::string()));
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
    // Same normalization as when loading the file: any stray byte in the
    // queried name (\r, NBSP, NUL, uppercase) is discarded before
    // comparing instead of being allowed to break the lookup.
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
    // Records in the log WHAT was saved and WHERE: if the player still
    // cannot use commands, the next log line shows whether the stored
    // name differs from the one queried later.
    utils::MainLogger::get().info(std::string("/") + (op ? "op " : "deop ") + key +
                                  (op ? " -> operator" : " -> not operator") + " (" +
                                  std::to_string(ops_.size()) + " in " + dataPath_ + "/ops.txt)");
}

// Helper: ensure "<dataPath>/players/" exists.
static void ensurePlayersDir(const std::string& dataPath) {
    std::string dir = dataPath + "/players";
    // Create if missing (best-effort; ignores EEXIST).
    ::mkdir(dir.c_str(), 0755);
}

// Helper: build to default NBT compound for to new player.
static NbtPtr buildDefaultPlayerNbt(Server* srv, const std::string& name) {
    mcp_tag_t* root = mcp_compound_tag_new_named("");
    mcp_compound_tag_set_long(root, "firstPlayed", (int64_t)(mcp_utils_time_ms()));
    mcp_compound_tag_set_long(root, "lastPlayed", (int64_t)(mcp_utils_time_ms()));
    mcp_compound_tag_set_short(root, "Health", 20);
    mcp_compound_tag_set_short(root, "MaxHealth", 20);
    mcp_compound_tag_set_short(root, "Hunger", 20);
    mcp_compound_tag_set_short(root, "Air", 300);
    mcp_compound_tag_set_int(root, "playerGameType", srv->gamemode());
    mcp_compound_tag_set_string(root, "NameTag", name.c_str());
    mcp_compound_tag_set_string(root, "Level", "world");
    if (srv->getDefaultWorld()) {
        auto sp = srv->getDefaultWorld()->spawn();
        mcp_compound_tag_set_int(root, "SpawnX", (int32_t)sp.x);
        mcp_compound_tag_set_int(root, "SpawnY", (int32_t)sp.y);
        mcp_compound_tag_set_int(root, "SpawnZ", (int32_t)sp.z);
    }
    return NbtPtr(root, [](mcp_tag_t* t) {
        if (t)
            mcp_tag_destroy(t);
    });
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

    // Nukkit-style player.dat: GZIP-compressed big-endian NBT.
    // Fall back to plain little-endian for legacy CurseStone files.
    mcp_tag_t* root = nullptr;
    {
        // Try GZIP + big-endian first.
        z_stream zs{};
        if (inflateInit2(&zs, 15 | 16) == Z_OK) {
            zs.next_in = (Bytef*)buf.data();
            zs.avail_in = (uInt)buf.size();
            std::vector<uint8_t> inflated;
            inflated.reserve(buf.size() * 4);
            uint8_t chunk[65536];
            int rc = Z_OK;
            do {
                zs.next_out = chunk;
                zs.avail_out = sizeof(chunk);
                rc = inflate(&zs, Z_NO_FLUSH);
                if (rc != Z_OK && rc != Z_STREAM_END)
                    break;
                size_t produced = sizeof(chunk) - zs.avail_out;
                if (produced > 0)
                    inflated.insert(inflated.end(), chunk, chunk + produced);
            } while (rc != Z_STREAM_END && zs.avail_in > 0);
            inflateEnd(&zs);
            if (rc == Z_STREAM_END && !inflated.empty()) {
                root = mcp_nbt_read_bytes(inflated.data(), inflated.size(),
                                          /*little_endian=*/false);
            }
        }
    }
    if (!root) {
        // Legacy: try plain big-endian (uncompressed).
        root = mcp_nbt_read_bytes(buf.data(), buf.size(), /*little_endian=*/false);
    }
    if (!root) {
        // Legacy: try plain little-endian (uncompressed, old CurseStone).
        root = mcp_nbt_read_bytes(buf.data(), buf.size(), /*little_endian=*/true);
    }
    if (!root) {
        utils::MainLogger::get().warn("Corrupt player data: " + path);
        return buildDefaultPlayerNbt(this, name);
    }
    return NbtPtr(root, [](mcp_tag_t* t) {
        if (t)
            mcp_tag_destroy(t);
    });
}

void Server::saveOfflinePlayerData(const std::string& name, NbtPtr nbt) {
    if (!nbt)
        return;
    ensurePlayersDir(dataPath_);
    std::string path = dataPath_ + "/players/" + name + ".dat";
    size_t outLen = 0;
    uint8_t* bytes = mcp_nbt_write_bytes(nbt.get(), /*little_endian=*/false, &outLen);
    if (!bytes || outLen == 0) {
        utils::MainLogger::get().warn("Failed to serialize NBT for " + name);
        if (bytes)
            free(bytes);
        return;
    }
    // GZIP-compress to match Nukkit player.dat layout.
    z_stream zs{};
    if (deflateInit2(&zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 15 | 16, 8, Z_DEFAULT_STRATEGY) !=
        Z_OK) {
        free(bytes);
        utils::MainLogger::get().warn("Failed to gzip player data for " + name);
        return;
    }
    size_t bound = deflateBound(&zs, (uLong)outLen);
    std::vector<uint8_t> gz(bound);
    zs.next_in = (Bytef*)bytes;
    zs.avail_in = (uInt)outLen;
    zs.next_out = (Bytef*)gz.data();
    zs.avail_out = (uInt)gz.size();
    int rc = deflate(&zs, Z_FINISH);
    deflateEnd(&zs);
    free(bytes);
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

// Nukkit-compat method implementations
Player* Server::getPlayerExact(const std::string& name) const {
    auto snap = playersSnapshot();
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
    for (const auto& p : playersSnapshot()) {
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

// --- Nukkit-compat: World management ---
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
    // Nukkit: called when to player successfully logs in.
    // Used to broadcast join message + update player list.
    // We handle this in player::handleLogin already.
}

std::string Server::getConfigString(const std::string& key, const std::string& def) const {
    // Stub — full config accessor would read from server.properties
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

void Server::broadcastPacketsCallback(const std::vector<uint8_t>& /*data*/,
                                      const std::vector<std::string>& /*identifiers*/) {
    // Stub — would batch-send to pre-encoded packet to specific players.
}

void Server::addWhitelist(const std::string& /*name*/) {
    // Stub — whitelist not implemented yet.
}

void Server::removeWhitelist(const std::string& /*name*/) {
    // Stub
}

void Server::reloadWhitelist() {
    // Stub
}

void Server::saveOfflinePlayerData(const std::string& name, NbtPtr nbt, bool /*async*/) {
    saveOfflinePlayerData(name, nbt);
}

void Server::titleTick() {
    // Update MOTD with player count + TPS (like Nukkit does)
    if (network_) {
        std::string motd = "MCPE;" + motd_ + ";" + std::to_string(PROTOCOL_VERSION) + ";" +
                           std::string(MINECRAFT_VERSION) + ";" +
                           std::to_string(playersSnapshot().size()) + ";" +
                           std::to_string(max_players_);
        // Cast to RakLibInterface to access setServerName
        auto* raklib = dynamic_cast<network::RakLibInterface*>(network_->getRakLib());
        if (raklib)
            raklib->setServerName(motd);
    }
}

void Server::sendRecipeList(Player* player) {
    // Nukkit: sends the full crafting recipe list to to player.
    // current implementation: builds to CraftingDataPacket with ALL
    // loaded recipes and sends it to this player (doFirstSpawn already
    // sends it on its own; this method remains for manual re-syncs,
    // just like Nukkit's sendRecipeList()).
    if (!player || !crafting_)
        return;
    auto cd = std::make_shared<network::protocol::CraftingDataPacket>();
    cd->addAllRecipes(*crafting_);
    player->sendPacket(cd);
}

void Server::updatePlayerListData(const std::string& /*uuid*/, const std::string& /*name*/,
                                  int64_t /*eid*/, const std::string& /*skin*/) {
    // Nukkit: broadcasts PlayerListPacket ADD to all online players.
    // We handle this in player::doFirstSpawn already.
}

void Server::removePlayerListData(const std::string& /*uuid*/) {
    // Nukkit: broadcasts PlayerListPacket REMOVE to all online players.
    // We handle this in player::close already.
}

void Server::sendFullPlayerListData(Player* /*target*/) {
    // Nukkit: sends the full player list to to specific player.
    // We handle this in player::doFirstSpawn already.
}

} // namespace cursestone
