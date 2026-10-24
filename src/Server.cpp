/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  | \__ \  __/\__/ / || (_) | | | |  __/
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
// Server singleton implementation.
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
#include "plugin/PluginManager.h" // Lua plugin support
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

} // namespace

void Server::loadConfig() {
    using utils::Config;
    Config props; // Basic server.properties settings.
    Config adv;   // Advanced cursestone.yml settings.

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

    // Load server.properties settings.
    ip_ = props.getString("server-ip", "0.0.0.0");
    port_ = (uint16_t)props.getInt("server-port", 19132);
    motd_ = props.getString("motd", "CurseStone");
    max_players_ = (int)props.getInt("max-players", 20);
    gamemode_ = (int)props.getInt("gamemode", 0);
    difficulty_ = (int)props.getInt("difficulty", 1);
    whitelist_ = readBool(props, "white-list", false);
    online_mode_ = readBool(props, "online-mode", false);
    view_distance_ = (int)props.getInt("view-distance", 8);

    // Load cursestone.yml settings.
    async_workers_ = (int)adv.getInt("settings.async-workers", 4);
    autosave_ticks_ = (int)adv.getInt("settings.autosave-ticks", 6000);
    advanced_log_ = readBool(adv, "settings.advanced-log", true);
    keep_inventory_ = readBool(adv, "gameplay.keep-inventory", false);
    bed_spawn_enabled_ = readBool(adv, "gameplay.bed-spawn", true);

    // Apply memory limits before the tick loop starts.
    MemoryManager::get().setLimitMB((int)adv.getInt("memory.max-mb", 512));
    MemoryManager::get().setGcIntervalTicks(
        (uint64_t)std::max<int64_t>(20, adv.getInt("memory.gc-interval-ticks", 200)));

    // Plugin limits support zero to disable each limit and root-level fallbacks.
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
    utils::MainLogger::get().info(std::string("Keep inventory: ") +
                                  (keep_inventory_ ? "on" : "off"));
    utils::MainLogger::get().info(std::string("Bed spawn: ") +
                                  (bed_spawn_enabled_ ? "on" : "off"));
    utils::MainLogger::get().info(
        "Plugin limits: instructions=" + std::to_string(plugin::LuaPlugin::limits().instructions) +
        ", time-ms=" + std::to_string(plugin::LuaPlugin::limits().timeMs) +
        ", max-errors=" + std::to_string(plugin::LuaPlugin::limits().maxErrors) + " (0 = off)");
}

void Server::initLogger() {
    // Write server.log to the data directory in real time.
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
    // Keep a raw pointer to start the interface after transferring ownership.
    auto* rli_ptr = rli.get();
    network_->registerInterface(std::move(rli));
    rli_ptr->start();
    utils::MainLogger::get().info("Network bound to " + ip_ + ":" + std::to_string(port_));
}

void Server::loadDefaultWorld() {
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
    lc.dataPath = dataPath_; // Allows World to resolve absolute world paths.

    // Create world directories before constructing the WorldProvider.
    // This mirrors Nukkit's Anvil directory layout.
    std::string worldsDir = dataPath_ + "/worlds";
    std::string worldDir = worldsDir + "/" + lc.name;
    std::string regionDir = worldDir + "/region";
    ::mkdir(worldsDir.c_str(), 0755);
    ::mkdir(worldDir.c_str(), 0755);
    ::mkdir(regionDir.c_str(), 0755);

    // Construct the World and initialize its provider.
    world_ = std::make_shared<world::World>(std::move(lc));
    worlds_.push_back(world_);

    utils::MainLogger::get().info("World directory: " + worldDir);
    utils::MainLogger::get().info("Region directory: " + regionDir);
}

// ---------------------------------------------------------------------
// Multiworld loading and unloading during runtime.
// ---------------------------------------------------------------------
// The default world is stored in world_ and worlds_[0]; plugin worlds are stored in worlds_ too.

World* Server::loadWorld(const std::string& name, const std::string& generator) {
    if (name.empty())
        return nullptr;
    // Return the existing world without modifying it.
    if (World* existing = getWorldByName(name))
        return existing;

    world::WorldConfig lc;
    lc.name = name;
    lc.format = "anvil";
    lc.generator = generator.empty() ? "normal" : generator;
    lc.seed = (int64_t)mcp_utils_time_ms();
    lc.view_distance = view_distance_;
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

        // Move players to the default world's spawn before releasing the world.
        for (auto& p : playersSnapshot()) {
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

    // Initialize logging before loading configuration.
    initLogger();

    // Create any missing configuration files from built-in defaults.
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
    // Built-in recipes are registered in CraftingManager::registerBuiltinRecipes().
    int recipeCount = crafting_->registerBuiltinRecipes();
    utils::MainLogger::get().info("Loaded " + std::to_string(recipeCount) + " crafting recipes");
    async_pool_ = std::make_shared<scheduler::AsyncPool>(async_workers_);
    utils::MainLogger::get().info("Opening network interface...");
    openNetwork();
    utils::MainLogger::get().info("Loading default world...");
    loadDefaultWorld();
    // Load Lua plugins after the world exists and before console input starts.
    utils::MainLogger::get().info("Loading plugins...");
    plugin::PluginManager::get().loadAll(dataPath_ + "/plugins", dataPath_ + "/data_pl");
    utils::MainLogger::get().info("Starting console command reader...");
    startCommandReader();

    // Calculate total startup time.
    auto boot_end = std::chrono::steady_clock::now();
    auto boot_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(boot_end - boot_start).count();
    double boot_sec = (double)boot_ms / 1000.0;
    utils::MainLogger::get().info("Server ready Tick loop starting...");
    utils::MainLogger::get().info("Server started in " + std::to_string(boot_sec) + "s");

    // Allow filesystem buffers to flush before the tick loop starts.
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
}

void Server::runTickLoop() {
    uint64_t last_tick_count = 0;
    auto last_tps_time = std::chrono::steady_clock::now();
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
        auto elapsed = std::chrono::steady_clock::now() - start_ts;
        auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
        next_tick += std::chrono::milliseconds(CURSESTONE_TICK_MS);
        if (std::chrono::steady_clock::now() < next_tick) {
            std::this_thread::sleep_until(next_tick);
        } else {
            // Re-anchor after an overrun to prevent chained catch-up ticks.
            next_tick = std::chrono::steady_clock::now();
        }
        (void)elapsed_ms;
        // Update TPS once per second.
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
}

void Server::tick() {
    ++currentTick_;

    // Update the current tick for crash diagnostics.
    diagnostics::Diagnostics::get().setTick(currentTick_);

    // Run the Workbench tick-start hook.
    auto wb_start = std::chrono::steady_clock::now();
    WorkbenchManager::get().onTickStart();

    // Remove players marked for deletion.
    {
        std::lock_guard<std::mutex> lk(players_mtx_);
        players_.erase(std::remove_if(players_.begin(), players_.end(),
                                      [](const std::shared_ptr<Player>& p) {
                                          return !p || p->isPendingRemoval();
                                      }),
                       players_.end());
    }

    // Tick the scheduler to process synchronous tasks and async completions.
    auto t0 = std::chrono::steady_clock::now();
    scheduler::ServerScheduler::get().tick(currentTick_);
    auto t1 = std::chrono::steady_clock::now();
    WorkbenchManager::get().onTickPhase(
        "scheduler",
        (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count());

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
        auto psnap = playersSnapshot();
        for (auto& p : psnap) {
            if (p)
                p->saveData(); // Save player NBT data.
        }
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

void Server::shutdown() {
    // Defer cleanup until the tick loop exits to avoid cross-thread races.
    if (running_.exchange(false, std::memory_order_relaxed)) {
        utils::MainLogger::get().info("Server shutting down...");
        std::thread([this]() {
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
    // Mirror broadcasts to the console without formatting codes.
    utils::MainLogger::get().info("[CHAT] " + TC::clean(msg));
}
void Server::broadcastPacket(void* /*p*/) {
    // Send packets through each player's source interface.
}
int64_t Server::allocateEid() {
    return next_eid_.fetch_add(1, std::memory_order_relaxed);
}
void Server::saveAll() {
    // Save player data first so world changes are marked dirty.
    auto snap = playersSnapshot();
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

// Incremental autosave pipeline.
static constexpr size_t kAutosaveChunksPerTick = 64;
static constexpr uint64_t kAutosaveMsPerTick = 3;
// Stop retrying after this many consecutive idle ticks.
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

    // Write level.dat once without processing chunks.
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
        return; // No dirty chunks to drain.

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
        utils::MainLogger::get().info(
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
    size_t weird = 0;
    while (pos <= content.size()) {
        size_t nl = content.find('\n', pos);
        std::string raw =
            (nl == std::string::npos) ? content.substr(pos) : content.substr(pos, nl - pos);
        pos = (nl == std::string::npos) ? content.size() + 1 : nl + 1;
        std::string line = normalizeOpName(raw);
        if (line.empty())
            continue; // Skip blank or invalid-only lines.
        if (!looksLikeValidOpName(line)) {
            ++weird;
            // Keep invalid entries but warn that they cannot match login names.
            utils::MainLogger::get().warn("ops.txt entry cannot match any "
                                          "player name (kept anyway): '" +
                                          line + "' in " + ops_path);
        }
        ops_.insert(std::move(line));
    }

    // Log recognized names to help diagnose operator lookup problems.
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

    // Read Nukkit-compatible GZIP-compressed big-endian NBT first.
    // Fall back to uncompressed formats used by legacy CurseStone files.
    mcp_tag_t* root = nullptr;
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
                root = mcp_nbt_read_bytes(inflated.data(), inflated.size(),
                                          /*little_endian=*/false);
            }
        }
    }
    if (!root) {
        // Try uncompressed big-endian NBT.
        root = mcp_nbt_read_bytes(buf.data(), buf.size(), /*little_endian=*/false);
    }
    if (!root) {
        // Try legacy uncompressed little-endian NBT.
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
    // Compress player data using GZIP for Nukkit compatibility.
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

// Nukkit-compatible method implementations.
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

// Nukkit-compatible world management.
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
    // Nukkit callback for successful player login.
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

void Server::broadcastPacketsCallback(const std::vector<uint8_t>& /*data*/,
                                      const std::vector<std::string>& /*identifiers*/) {
    // Stub for sending encoded packets to selected players in batches.
}

void Server::addWhitelist(const std::string& /*name*/) {
    // Stub: whitelist support is not implemented.
}

void Server::removeWhitelist(const std::string& /*name*/) {
    // Stub: whitelist support is not implemented.
}

void Server::reloadWhitelist() {
    // Stub: whitelist support is not implemented.
}

void Server::saveOfflinePlayerData(const std::string& name, NbtPtr nbt, bool /*async*/) {
    saveOfflinePlayerData(name, nbt);
}

void Server::titleTick() {
    // Update the MOTD with player count and TPS.
    if (network_) {
        std::string motd = "MCPE;" + motd_ + ";" + std::to_string(PROTOCOL_VERSION) + ";" +
                           std::string(MINECRAFT_VERSION) + ";" +
                           std::to_string(playersSnapshot().size()) + ";" +
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