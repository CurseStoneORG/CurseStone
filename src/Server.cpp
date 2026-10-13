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
// Server singleton implementation (Phase 21 completion).
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
#include <thread>
#include <chrono>
#include <cstdlib>   // std::_Exit
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
#include "plugin/PluginManager.h"   // plugins Lua (fase 3)

#include <iostream>
#include <fstream>
#include <chrono>
#include <thread>
#include <algorithm>
#include <sys/stat.h>
#include <sys/types.h>
#include <cctype>

namespace cursestone {

Server::Server() = default;
Server::~Server() = default;

void Server::loadConfig() {
    using utils::Config;
    Config cfg;

    // First try PMMP-style server.properties (key=value INI file).
    // If it doesn't exist, fall back to cursestone.yml.
    std::string props_path = dataPath_ + "/server.properties";
    std::string yaml_path  = dataPath_ + "/cursestone.yml";
    bool loaded_props = false;
    if (cfg.loadIni(props_path)) {
        loaded_props = true;
        utils::MainLogger::get().info("Loaded config: " + props_path);
    } else if (cfg.loadYaml(yaml_path)) {
        utils::MainLogger::get().info("Loaded config: " + yaml_path);
    } else {
        utils::MainLogger::get().info("No config found; using defaults. " +
            std::string("Either create ") + props_path + " or " + yaml_path);
    }

    // Getters accept dotted or flat keys; for the INI form, the key is
    // just the property name (e.g. "server-port"). For YAML, the key is
    // the dotted path (e.g. "server.port"). Config::getString accepts both.
    auto get_str = [&](const char* yaml_key, const char* ini_key,
                       const char* def) -> std::string {
        // Try INI form first, then YAML, then default.
        std::string v = cfg.getString(ini_key, std::string());
        if (!v.empty()) return v;
        return cfg.getString(yaml_key, def);
    };
    auto get_int = [&](const char* yaml_key, const char* ini_key,
                       int64_t def) -> int64_t {
        int64_t v = cfg.getInt(ini_key, INT64_MIN);
        if (v != INT64_MIN) return v;
        return cfg.getInt(yaml_key, def);
    };
    auto get_bool = [&](const char* yaml_key, const char* ini_key,
                        bool def) -> bool {
        // Fix: check if the INI key EXISTS before checking its value.
        // The old code did `if (v != def)` which meant that if the user
        // set whitelist=false and the default was also false, the INI
        // value was ignored and the YAML was read instead.
        // New approach: try to get the INI key as a string. If it's
        // non-empty, the key exists → parse it as bool.
        std::string ini_val = cfg.getString(ini_key, std::string());
        if (!ini_val.empty()) {
            // Key exists in INI — use it.
            std::string lower = ini_val;
            std::transform(lower.begin(), lower.end(), lower.begin(),
                          [](unsigned char c){ return std::tolower(c); });
            return (lower == "true" || lower == "on" || lower == "1" ||
                    lower == "yes" || lower == "y");
        }
        // Key not in INI — fall back to YAML.
        return cfg.getBool(yaml_key, def);
    };

    ip_            = get_str("server.ip", "server-ip", "0.0.0.0");
    port_          = (uint16_t)get_int("server.port", "server-port", 19132);
    motd_          = get_str("server.motd", "motd", "CurseStone");
    max_players_   = (int)get_int("server.max-players", "max-players", 20);
    gamemode_      = (int)get_int("server.gamemode", "gamemode", 0);
    difficulty_    = (int)get_int("server.difficulty", "difficulty", 1);
    whitelist_     = get_bool("server.whitelist", "white-list", false);
    online_mode_   = get_bool("server.online-mode", "online-mode", false);
    advanced_log_  = get_bool("server.advanced-log", "advanced-log", true);
    view_distance_ = (int)get_int("server.view-distance", "view-distance", 8);
    async_workers_ = (int)get_int("settings.async-workers", "async-workers", 4);
    autosave_ticks_ = (int)get_int("server.autosave-ticks", "autosave-ticks", 6000);

    // Memory budget for the global GarbageCollector. Previously this key
    // existed in cursestone.yml but was never read — MemoryManager silently
    // ran on its hardcoded 512 MB default. Also expose the GC pass
    // interval (ticks between coordinated sweeps; 200 ticks = 10s at
    // 20 TPS). Must be set BEFORE the tick loop starts so the very first
    // collect() already honors the operator's budget.
    MemoryManager::get().setLimitMB(
        (int)get_int("memory.max-mb", "max-mb", 512));
    MemoryManager::get().setGcIntervalTicks(
        (uint64_t)std::max<int64_t>(
            20, get_int("memory.gc-interval-ticks", "gc-interval-ticks", 200)));

    // If we just loaded the .properties file (no YAML), create the
    // server.properties in the data dir from the default resource so the
    // user can edit it on next startup.
    if (loaded_props) {
        utils::MainLogger::get().info("Server port: " + std::to_string(port_));
        utils::MainLogger::get().info("Server IP: " + ip_);
        utils::MainLogger::get().info("MOTD: " + motd_);
        utils::MainLogger::get().info("Max players: " + std::to_string(max_players_));
        utils::MainLogger::get().info("Gamemode: " + std::to_string(gamemode_) +
            " (0=survival, 1=creative, 2=adventure, 3=spectator)");
        utils::MainLogger::get().info("World generator: " +
            cfg.getString("generator", "normal"));
        utils::MainLogger::get().info("World format: " +
            cfg.getString("world-format", "anvil"));
    }
}

void Server::initLogger() {
    // Open server.log in the data directory so all log lines are written
    // to disk in real time (PMMP-equivalent: pocketmine.log).
    std::string log_path = dataPath_ + "/server.log";
    mcp_main_logger_init(log_path.c_str(), /*colored=*/true);
    utils::MainLogger::get().info(std::string(CURSESTONE_NAME) +
        " v" + CURSESTONE_VERSION + " (codename: " + CURSESTONE_CODENAME + ")");
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
    // Keep a raw pointer so we can call start() after the unique_ptr is moved
    // into the Network (which takes ownership). start() opens the UDP socket
    // and registers the MOTD; without it, the server binds nothing and clients
    // cannot connect.
    auto* rli_ptr = rli.get();
    network_->registerInterface(std::move(rli));
    rli_ptr->start();
    utils::MainLogger::get().info("Network bound to " + ip_ + ":" + std::to_string(port_));
}

void Server::loadDefaultWorld() {
    // Load world name/format/generator from server.properties or cursestone.yml.
    using utils::Config;
    Config cfg;
    cfg.loadIni(dataPath_ + "/server.properties");
    cfg.loadYaml(dataPath_ + "/cursestone.yml");

    // The world settings are the `world-*` keys (server.properties) and the
    // `world.*` block (cursestone.yml). The old `level-*` spellings are gone:
    // the last deprecated one, `level-name`, is now `world-name`.
    // server.properties still overrides cursestone.yml exactly as before.
    auto get_str = [&](const char* ini_key, const char* yaml_key,
                       const char* def) -> std::string {
        std::string v = cfg.getString(ini_key, std::string());
        if (!v.empty()) return v;
        return cfg.getString(yaml_key, def);
    };
    auto get_int = [&](const char* ini_key, const char* yaml_key,
                       int64_t def) -> int64_t {
        int64_t v = cfg.getInt(ini_key, INT64_MIN);
        if (v != INT64_MIN) return v;
        return cfg.getInt(yaml_key, def);
    };

    world::WorldConfig lc;
    lc.name      = get_str("world-name", "world.name", "world");
    lc.format    = get_str("world-format", "world.format", "anvil");
    // The generator keeps the precedence it always had: the plain INI
    // `generator` first, then cursestone.yml. `level-type` / `world-type` are
    // deliberately NOT read: the shipped server.properties sets
    // `level-type=DEFAULT`, and wiring that key to the generator would change
    // the world that existing servers load.
    lc.generator = cfg.getString("generator", std::string());
    if (lc.generator.empty()) {
        lc.generator = cfg.getString("world.generator", std::string());
        if (lc.generator.empty()) lc.generator = "normal";
    }
    int64_t seed = get_int("world-seed", "world.seed", 0);
    lc.seed = (seed != 0) ? (int64_t)seed : (int64_t)mcp_utils_time_ms();
    lc.spawn_x = 0; lc.spawn_y = 64; lc.spawn_z = 0;
    lc.view_distance = view_distance_;
    lc.dataPath = dataPath_;   // so World can compute absolute world paths

    // Create the world directory structure BEFORE constructing the World,
    // so the WorldProvider can open the region/ subdirectory on init().
    // Mirrors Nukkit's Anvil.generate(): worlds/<name>/ + worlds/<name>/region/
    std::string worldsDir = dataPath_ + "/worlds";
    std::string worldDir  = worldsDir + "/" + lc.name;
    std::string regionDir = worldDir + "/region";
    ::mkdir(worldsDir.c_str(), 0755);
    ::mkdir(worldDir.c_str(), 0755);
    ::mkdir(regionDir.c_str(), 0755);

    // Construct the World — this instantiates the WorldProvider (Anvil by
    // default) and calls provider->init(worldDir). The provider creates the
    // region/ subdir if missing and reads level.dat if it exists.
    world_ = std::make_shared<world::World>(std::move(lc));

    utils::MainLogger::get().info("World directory: " + worldDir);
    utils::MainLogger::get().info("Region directory: " + regionDir);
}

void Server::startCommandReader() {
    console_reader_ = std::make_unique<CommandReader>([this](const std::string& line) {
        if (line.empty()) return;
        ConsoleCommandSender sender;
        if (command_map_) command_map_->dispatch(&sender, line);
    });
    console_reader_->start();
}

void Server::start(const std::string& dataPath) {
    // Record the boot start time so we can log "Server started in N.Ns"
    // at the end. Mirrors PMMP's startup timing log.
    auto boot_start = std::chrono::steady_clock::now();

    dataPath_ = dataPath;
    running_.store(true, std::memory_order_relaxed);

    // Diagnostics: mark the server start so crashdumps can report uptime.
    diagnostics::Diagnostics::get().setUptimeStart(
        (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());

    // Ensure data dir exists.
    ::mkdir(dataPath_.c_str(), 0755);
    ::mkdir((dataPath_ + "/resources").c_str(), 0755);

    // Initialize logger first so we can see config-loading messages.
    initLogger();

    // If server.properties doesn't exist in data dir, copy the default
    // from <dataPath>/resources/server.properties so the user has a
    // template they can edit.
    std::string user_props = dataPath_ + "/server.properties";
    FILE* fcheck = fopen(user_props.c_str(), "rb");
    if (!fcheck) {
        // Copy from resources/server.properties (bundled with the binary).
        std::string src = dataPath_ + "/resources/server.properties";
        FILE* src_fp = fopen(src.c_str(), "rb");
        if (src_fp) {
            FILE* dst_fp = fopen(user_props.c_str(), "wb");
            if (dst_fp) {
                char buf[4096];
                size_t n;
                while ((n = fread(buf, 1, sizeof(buf), src_fp)) > 0) {
                    fwrite(buf, 1, n, dst_fp);
                }
                fclose(dst_fp);
                utils::MainLogger::get().info("Created default server.properties at " + user_props);
            }
            fclose(src_fp);
        }
    } else {
        fclose(fcheck);
    }

    utils::MainLogger::get().info("Loading configuration...");
    loadConfig();
    loadOps();
    utils::MainLogger::get().info("Registering blocks and items...");
    registerBlocksAndItems();
    command_map_ = std::make_shared<SimpleCommandMap>();
    registerDefaultCommands(*command_map_);
    crafting_ = std::make_shared<inventory::CraftingManager>();
    // Las recetas van EN CODIGO (CraftingManager::registerBuiltinRecipes):
    // no hay ningun archivo de recetas que cargar. Antes se leia uno
    // externo que no se empaqueta con el binario, asi que una release
    // limpia se quedaba sin ninguna receta.
    int recipeCount = crafting_->registerBuiltinRecipes();
    utils::MainLogger::get().info("Loaded " + std::to_string(recipeCount) +
        " crafting recipes");
    async_pool_ = std::make_shared<scheduler::AsyncPool>(async_workers_);
    utils::MainLogger::get().info("Opening network interface...");
    openNetwork();
    utils::MainLogger::get().info("Loading default world...");
    loadDefaultWorld();
    // Plugins Lua: entre el mundo y el lector de consola. El mundo ya
    // existe (los plugins pueden tocarlo) y la consola todavía no recibe
    // comandos (ningún comando llega mientras los plugins se registran).
    utils::MainLogger::get().info("Loading plugins...");
    plugin::PluginManager::get().loadAll(dataPath_ + "/plugins");
    utils::MainLogger::get().info("Starting console command reader...");
    startCommandReader();

    // Compute total boot time.
    auto boot_end = std::chrono::steady_clock::now();
    auto boot_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        boot_end - boot_start).count();
    double boot_sec = (double)boot_ms / 1000.0;
    utils::MainLogger::get().info("Server ready Tick loop starting...");
    utils::MainLogger::get().info("Server started in " +
        std::to_string(boot_sec) + "s");

    // Give the OS a brief moment to flush filesystem buffers after the
    // world directory + level.dat + provider init writes. Without this,
    // the tick loop can race with filesystem sync on slow devices
    // (Android especially). 200ms is plenty for any reasonable storage.
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
}

void Server::runTickLoop() {
    uint64_t last_tick_count = 0;
    auto last_tps_time = std::chrono::steady_clock::now();
    //
    // BUG ARREGLADO ("en Linux hay lag y las particulas/items no se
    // sincronizan; en Android todo va bien"): el bucle dormia
    // sleep_for(50 - trunc(elapsed_ms)). El truncado a milisegundos
    // enteros PERDIA la fraccion de cada tick (elapsed=30.9ms dormia
    // 20ms -> tick de 50.9ms) y el overshoot del scheduler de Linux
    // sumaba 1-3ms mas: la deriva se ACUMULABA y el servidor caia por
    // debajo de 20 TPS. Con TPS bajo la simulacion de items del server
    // va mas lenta que la del cliente (que integra a su propio 20fps):
    // un item que cae queda en un sitio en el server y en otro en el
    // cliente, los paquetes de particulas salen tarde y todo "arrastra".
    //
    // FIX: calendario ABSOLUTO (sleep_until). next_tick avanza 50ms por
    // tick SIEMPRE — el overshoot se recupera en el siguiente tick en vez
    // de acumularse, exactamente como hace PMMP/Genisys con su checkTick.
    auto next_tick = std::chrono::steady_clock::now();
    while (running_.load(std::memory_order_relaxed)) {
        // Poll the external stop flag (set by CurseStone.cpp's SIGINT/SIGTERM
        // handler) on the MAIN thread, in normal — not signal — context.
        // This is the only place that flag is ever acted on; see
        // setExternalStopFlag()'s comment for why the handler itself
        // can't call shutdown() directly.
        if (external_stop_flag_ && *external_stop_flag_ && running_.load(std::memory_order_relaxed)) {
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
            // Tick sobrepasado (frame lento): reanclar el calendario al
            // presente para no encadenar una racha de ticks atrasados
            // (catch-up en rabia) — igual que hace un servidor PMMP.
            next_tick = std::chrono::steady_clock::now();
        }
        (void)elapsed_ms;
        // Compute TPS once per second.
        if (currentTick_ - last_tick_count >= 20) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed_sec = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_tps_time).count();
            if (elapsed_sec > 0) tps_ = (int)(20000 / elapsed_sec);
            last_tps_time = now;
            last_tick_count = currentTick_;
        }
    }
    utils::MainLogger::get().info("Tick loop exited.");
    tick_loop_exited_.store(true, std::memory_order_relaxed);
    // Plugins Lua: ya salimos del loop (nadie dispara eventos) y todavía
    // se puede guardar el mundo — deshabilitar todos los plugins aquí.
    plugin::PluginManager::get().disableAll();

    // ====================================================================
    // Cleanup — runs on the MAIN thread, AFTER the tick loop exits.
    // This is critical: /stop is dispatched from the CommandReader
    // thread, so doing saveAll() inside Server::shutdown() would race
    // with the main thread destroying Server members (mutexes, world_,
    // players_). By deferring all cleanup to here, we guarantee that
    // saveAll() runs single-threaded before any destructor fires.
    // ====================================================================
    if (network_) network_->tick();   // graceful RakLib close
    // Final global GC pass BEFORE saveAll(): flushes tile/entities, saves
    // out-of-view chunks as part of the chunk sweep, and drops picked-up
    // item records so nothing stale is written back to disk. Forced —
    // rate limiting must not eat the last pass.
    MemoryManager::get().collect(/*force=*/true);
    saveAll();
    if (console_reader_) console_reader_->stop();

    // Stop and JOIN the chunk-generation pool here, on the main thread,
    // while the process is still fully alive. The pool is owned by this
    // Server singleton, and the order in which static destructors run is
    // not guaranteed: if a worker is still inside GenerationTask::run()
    // when the global block registry / chunk tables are destroyed, it dies
    // in mcp_block_light_opacity() reading a table that no longer exists —
    // a SIGSEGV logged right AFTER "Shutdown complete." (reproducible
    // whenever a player had chunks in flight at stop time). stop() joins
    // every worker and is idempotent, so the pool's own destructor stays a
    // no-op and nothing can be generating once we return.
    if (async_pool_) async_pool_->stop();

    utils::MainLogger::get().info("Shutdown complete.");
}

void Server::tick() {
    ++currentTick_;

    // Diagnostics: keep the current tick visible to crashdumps.
    diagnostics::Diagnostics::get().setTick(currentTick_);

    // --- Workbench: tick start hook ---
    auto wb_start = std::chrono::steady_clock::now();
    WorkbenchManager::get().onTickStart();

    // --- Reap players flagged for removal by Player::close() ---
    {
        std::lock_guard<std::mutex> lk(players_mtx_);
        players_.erase(
            std::remove_if(players_.begin(), players_.end(),
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
    // had finished but an AsyncTask (e.g. a chunk save) completed on
    // a worker thread, its onCompletion() would never run because
    // tick() itself was being skipped. Now we always call tick(); the
    // cost when there's genuinely nothing to do is two cheap
    // near-empty lock/unlock pairs, not worth special-casing away.
    auto t0 = std::chrono::steady_clock::now();
    scheduler::ServerScheduler::get().tick(currentTick_);
    auto t1 = std::chrono::steady_clock::now();
    WorkbenchManager::get().onTickPhase("scheduler",
        (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count());

    // --- Phase: world ---
    t0 = std::chrono::steady_clock::now();
    if (world_) world_->tick(currentTick_);
    t1 = std::chrono::steady_clock::now();
    WorkbenchManager::get().onTickPhase("level",
        (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count());

    // --- Phase: block entities (furnaces) ---
    // Fuera de World::tick para mantener esa fase libre de envíos de
    // packets (el horno manda progreso/contenido a viewers aquí).
    t0 = std::chrono::steady_clock::now();
    if (world_) world_->tickTiles(currentTick_);
    t1 = std::chrono::steady_clock::now();
    WorkbenchManager::get().onTickPhase("tiles",
        (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count());

    // --- Phase: global GC ---
    // THE single GarbageCollector pass for the whole server. Everything
    // that reclaims shared world memory funnels through here:
    // chunk unload sweep (was a separate 100-tick schedule in this very
    // function) + dropped-item despawn/taken sweep (was a per-tick sweep
    // inside World::tick) + heap trim. Centralizing matters: independent
    // reclaimer schedules were freeing world state (dropped items, tiles,
    // chunks) in the middle of player actions — the "player actions
    // desyncing from the server" symptom. Rate limiting inside
    // MemoryManager::collect() keeps the pass off every tick; it prices
    // like the old chunk sweep (occasional maintenance, may hit disk).
    t0 = std::chrono::steady_clock::now();
    MemoryManager::get().collect();
    t1 = std::chrono::steady_clock::now();
    WorkbenchManager::get().onTickPhase("gc",
        (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count());

    // --- Phase: network (first pass) ---
    // CRITICAL: process network BEFORE player updates.
    t0 = std::chrono::steady_clock::now();
    if (network_) network_->tick();
    t1 = std::chrono::steady_clock::now();
    WorkbenchManager::get().onTickPhase("network",
        (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count());

    // Update watchdog: main thread is alive
    WorkbenchManager::get().last_drain_time_ms.store(
        (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count(),
        std::memory_order_relaxed);

    // --- Phase: player updates ---
    auto snap = playersSnapshot();
    for (auto& p : snap) {
        if (!p) continue;
        if (p->isPendingRemoval()) continue;
        auto pt0 = std::chrono::steady_clock::now();
        p->onUpdate(currentTick_);
        auto pt1 = std::chrono::steady_clock::now();
        WorkbenchManager::get().onPlayerUpdate(p->getName(),
            (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(pt1 - pt0).count());
    }

    // Diagnostics: refresh the online-player snapshot (twice per second).
    // It is captured here, on the main thread, so the SIGSEGV handler can
    // read a ready-made snapshot instead of touching server state while
    // the process is dying.
    if (currentTick_ % 10 == 0) {
        uint64_t now_wall = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        std::vector<diagnostics::PlayerSnapshot> ps;
        ps.reserve(snap.size());
        for (auto& p : snap) {
            if (!p) continue;
            diagnostics::PlayerSnapshot s;
            s.name = p->getName();
            s.ip = p->ip();
            s.port = p->port();
            s.eid = p->runtimeId();
            auto pos = p->getNextPosition();
            s.x = pos.x; s.y = pos.y; s.z = pos.z;
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
    // ANTI-LAG: vuelca el buffer por tick de cada jugador como UN
    // BatchPacket comprimido por canal. Todo lo que este tick produjeron
    // los handlers de red, el world tick y las actualizaciones de jugador
    // sale aqui agrupado (ver la nota en Player::sendPacket).
    {
        auto bpf0 = std::chrono::steady_clock::now();
        for (auto& p : snap) {
            if (!p) continue;
            if (p->isPendingRemoval()) continue;
            p->flushPendingPackets();
        }
        auto bpf1 = std::chrono::steady_clock::now();
        WorkbenchManager::get().onTickPhase("batch-flush",
            (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(bpf1 - bpf0).count());
    }

    // --- Phase: network (second pass — flush outbound) ---
    if (network_) network_->tick();

    // --- Phase: auto-save (incremental) ---
    // ANTES: este bloque llamaba a world_->save(), que serializa TODOS los
    // chunks sucios en el mismo tick. Con el mundo en marcha son cientos
    // (todo lo generado desde el autosave anterior), y eso son varios
    // cientos de ms de hilo principal bloqueado: medido ~495 ms para 350
    // chunks y ~759 ms en el caso reportado, de los que ~90% es la
    // compresion zlib de los chunks (ver kChunkCompressionLevel en Anvil).
    // El cliente vivo lo lee como congelamiento/desconexion.
    // AHORA: el tick de autosave guarda lo barato (datos de jugador +
    // level.dat) y ABRE el drenaje; en los ticks siguientes se escribe la
    // cola a pocos chunks por tick con presupuesto de tiempo, asi el
    // coste por tick queda acotado y el mundo acaba entero en disco igual.
    if (autosave_ticks_ > 0 && currentTick_ % autosave_ticks_ == 0) {
        auto psnap = playersSnapshot();
        for (auto& p : psnap) {
            if (p) p->saveData();   // NBT de jugador: barato, no es I/O de chunks
        }
        beginAutosaveDrain();
    }
    if (autosave_draining_) tickAutosaveDrain();

    // --- Workbench: tick end hook ---
    auto wb_end = std::chrono::steady_clock::now();
    uint64_t total_ms = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
        wb_end - wb_start).count();
    WorkbenchManager::get().onTickEnd(total_ms);
}

void Server::shutdown() {
    // Signal the tick loop to exit. The actual cleanup (saveAll,
    // console_reader stop) is deferred to AFTER the tick loop exits
    // in runTickLoop(), because /stop may be dispatched from the
    // CommandReader thread. Doing saveAll() here would race with the
    // main thread's destructors (mutexes, world_, players_) — causing
    // "pthread_mutex_lock called on a destroyed mutex" and lost chunks.
    //
    // EMERGENCY WATCHDOG: if the main tick loop is frozen (e.g. by a
    // runaway synchronous operation), running_ is set to false but the
    // tick loop never gets to check it. /stop then hangs forever with
    // "Stopping the server..." printed and no exit. We spawn a short
    // watcher thread that gives the main loop 5 seconds to exit
    // gracefully; if it doesn't, we _exit() the process so the user
    // at least gets a prompt back. Data loss is acceptable in this
    // scenario — the alternative is a permanently hung server.
    if (running_.exchange(false, std::memory_order_relaxed)) {
        utils::MainLogger::get().info("Server shutting down...");
        std::thread([this]() {
            // Give the main loop 5 seconds to exit cleanly.
            for (int i = 0; i < 50; ++i) {
                if (!running_.load(std::memory_order_relaxed) &&
                    tick_loop_exited_.load(std::memory_order_relaxed)) {
                    return;  // main loop exited cleanly
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            // Timeout — force exit. We can't call ~Server() safely here
            // because we're on a different thread and the destructors
            // may race, but _exit() bypasses destructors entirely.
            utils::MainLogger::get().error(
                "Shutdown watchdog: tick loop did not exit within 5s — "
                "forcing process exit (unsaved chunks may be lost).");
            std::_Exit(1);
        }).detach();
    }
}

Player* Server::getPlayer(const std::string& name) const {
    auto snap = playersSnapshot();
    for (const auto& p : snap)
        if (p && p->getName() == name) return p.get();
    return nullptr;
}
void Server::addPlayer(std::shared_ptr<Player> p) {
    std::lock_guard<std::mutex> lk(players_mtx_);
    players_.push_back(std::move(p));
}
void Server::removePlayer(Player* p) {
    std::lock_guard<std::mutex> lk(players_mtx_);
    players_.erase(std::remove_if(players_.begin(), players_.end(),
        [p](const std::shared_ptr<Player>& q) { return q.get() == p; }), players_.end());
}
std::vector<std::shared_ptr<Player>> Server::playersSnapshot() const {
    std::lock_guard<std::mutex> lk(players_mtx_);
    return players_;
}
void Server::broadcastMessage(const std::string& msg) {
    auto snap = playersSnapshot();
    for (auto& p : snap) if (p) p->sendMessage(msg);
    // FIX ("los mensajes del chat nunca llegan a la consola"): every
    // broadcast (player chat, join/quit/death messages, server say) is
    // mirrored to the server console with the §-codes stripped — exactly
    // like PMMP's Server::broadcastMessage logging behaviour, so an admin
    // watching the console sees the same chat the players see in-game.
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
        if (p) p->saveData();
    }
    utils::MainLogger::get().info("[SAVE] " + std::to_string(snap.size()) +
        " player(s) saved.");

    // 2. Save the default world — flushes dirty chunks to .mca files
    //    AND writes level.dat (seed/spawn/time) via the provider.
    if (world_) {
        int saved = world_->save();
        utils::MainLogger::get().info("[SAVE] World saved (" +
            std::to_string(saved) + " chunks flushed).");
    }
}

// ============================================================
// Autosave incremental
// ============================================================
//
// Presupuesto por tick del drenaje. Medido con el codigo real —
// ChunkManager + Anvil (encode NBT + zlib + escritura en la region)
// sobre 400 chunks generados por el Normal de verdad (53 KB de NBT por
// chunk):
//   * nivel 6 de zlib: 0.72 ms/chunk  (el default viejo)
//   * nivel 1 de zlib: 0.23 ms/chunk  (kChunkCompressionLevel)
//   * escritura en la region: 0.008 ms/chunk
// Asi que 8 chunks ~ 1.9 ms de hilo principal por tick, y el tope de
// 6 ms recorta la lista si algun chunk sale mas caro (muchos tiles,
// chunks enormes). Los 400 chunks se drenaron en 50 ticks con un maximo
// de 3.4 ms en un solo tick, contra ~304 ms de golpe con el codigo
// viejo (el caso reportado de 759 ms es la misma cuenta con el mundo
// mas cargado).
static constexpr size_t   kAutosaveChunksPerTick = 8;
static constexpr uint64_t kAutosaveMsPerTick     = 6;
// Si el flush no consigue escribir nada durante este numero de ticks
// seguidos (p. ej. un provider que falla siempre), se cierra el drenaje
// en vez de quedarse reintentando el mismo chunk sucio para siempre.
static constexpr int      kAutosaveIdleTicksMax  = 40;

static uint64_t nowSteadyMs() {
    return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

void Server::beginAutosaveDrain() {
    autosave_draining_ = false;
    if (!world_) return;

    autosave_drain_start_ms_     = nowSteadyMs();
    autosave_drain_written_      = 0;
    autosave_drain_ticks_        = 0;
    autosave_drain_max_tick_ms_  = 0;
    autosave_drain_idle_ticks_   = 0;

    // level.dat (hora del mundo + spawn) se escribe AQUI, una vez, con
    // presupuesto 0 chunks: es un archivo diminuto y no queremos
    // reescribirlo en cada tick del drenaje.
    auto t0 = std::chrono::steady_clock::now();
    world_->saveBudget(/*max_chunks=*/0, /*max_ms=*/0,
                       /*write_level_data=*/true);
    WorkbenchManager::get().onAutosave((uint64_t)std::chrono::duration_cast<
        std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count());

    size_t pending = world_->unsavedChunkCount();
    if (pending == 0) return;   // nada sucio: ni abrimos el drenaje

    autosave_draining_ = true;
    if (advanced_log_) {
        utils::MainLogger::get().info("[SAVE] Autosave: " +
            std::to_string(pending) + " chunk(s) en cola -> drenando " +
            std::to_string(kAutosaveChunksPerTick) + "/tick (max " +
            std::to_string(kAutosaveMsPerTick) + "ms/tick).");
    }
}

void Server::tickAutosaveDrain() {
    if (!world_) { autosave_draining_ = false; return; }

    auto t0 = std::chrono::steady_clock::now();
    int saved = world_->saveBudget(kAutosaveChunksPerTick, kAutosaveMsPerTick,
                                   /*write_level_data=*/false);
    uint64_t tick_ms = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0).count();

    autosave_drain_written_ += (size_t)saved;
    ++autosave_drain_ticks_;
    if (tick_ms > autosave_drain_max_tick_ms_) autosave_drain_max_tick_ms_ = tick_ms;
    WorkbenchManager::get().onAutosave(tick_ms);
    if (tick_ms > 50) {
        utils::MainLogger::get().warn("[SAVE] Autosave flush took " +
            std::to_string(tick_ms) + "ms on one tick — this may cause "
            "client lag spikes!");
    }

    autosave_drain_idle_ticks_ = (saved == 0) ? (autosave_drain_idle_ticks_ + 1) : 0;
    size_t pending = world_->unsavedChunkCount();
    bool stuck = (pending > 0 && autosave_drain_idle_ticks_ >= kAutosaveIdleTicksMax);
    if (pending == 0 || stuck) {
        autosave_draining_ = false;
        uint64_t total_ms = nowSteadyMs() - autosave_drain_start_ms_;
        if (stuck) {
            utils::MainLogger::get().warn("[SAVE] Autosave drain stopped with " +
                std::to_string(pending) + " chunk(s) still dirty (provider "
                "not accepting writes?) — will retry on the next autosave.");
        } else if (autosave_drain_written_ > 0) {
            utils::MainLogger::get().info("[SAVE] Autosave done: " +
                std::to_string(autosave_drain_written_) + " chunk(s) en " +
                std::to_string(total_ms) + "ms (" +
                std::to_string(autosave_drain_ticks_) + " tick(s), max " +
                std::to_string(autosave_drain_max_tick_ms_) + "ms en un tick).");
        }
    }
}

permission::BanList& Server::getNameBans() {
    if (!name_bans_) {
        name_bans_ = std::make_unique<permission::BanList>(
            dataPath_ + "/banned-players.json");
        name_bans_->load();
    }
    return *name_bans_;
}

permission::BanList& Server::getIPBans() {
    if (!ip_bans_) {
        ip_bans_ = std::make_unique<permission::BanList>(
            dataPath_ + "/banned-ips.json");
        ip_bans_->load();
    }
    return *ip_bans_;
}

// ============================================================
// Operator list — backed by <dataPath>/ops.txt (one name per line)
// ============================================================
void Server::loadOps() {
    ops_.clear();
    // Lectura binaria: en modo texto un BOM UTF-8 llegaria igual, pero asi el
    // archivo se lee byte a byte y podemos quitarlo nosotros.
    std::ifstream in(dataPath_ + "/ops.txt", std::ios::binary);
    if (!in.is_open()) return;
    std::string content((std::istreambuf_iterator<char>(in)),
                         std::istreambuf_iterator<char>());

    // Un editor que guarda en "UTF-8 con BOM" (Notepad en Windows, varios
    // editores de Android) deja los 3 bytes 0xEF 0xBB 0xBF al principio. Sin
    // quitarlos, la PRIMERA entrada guardada es "\xEF\xBB\xBFdr1xy" y NUNCA
    // coincide con el nombre del jugador: el archivo se ve perfecto, el log
    // dice "Loaded 1 operator(s)" y aun asi ese op no puede usar comandos.
    if (content.size() >= 3 &&
        (unsigned char)content[0] == 0xEF &&
        (unsigned char)content[1] == 0xBB &&
        (unsigned char)content[2] == 0xBF) {
        content.erase(0, 3);
    }

    // Recorta espacios (incluidos tabs) de AMBOS extremos de cada linea. Antes
    // solo se recortaba la cola, asi que una linea " Dr1xy" tampoco coincidia.
    auto trim = [](std::string& s) {
        auto is_ws = [](char c) {
            return c == ' ' || c == '\t' || c == '\r' || c == '\n' ||
                   c == '\v' || c == '\f';
        };
        size_t b = 0, e = s.size();
        while (b < e && is_ws(s[b])) ++b;
        while (e > b && is_ws(s[e - 1])) --e;
        s = s.substr(b, e - b);
    };

    size_t pos = 0;
    while (pos <= content.size()) {
        size_t nl = content.find('\n', pos);
        std::string line = (nl == std::string::npos)
            ? content.substr(pos) : content.substr(pos, nl - pos);
        pos = (nl == std::string::npos) ? content.size() + 1 : nl + 1;
        trim(line);
        if (line.empty()) continue;
        for (char& c : line) c = (char)std::tolower((unsigned char)c);
        ops_.insert(std::move(line));
    }

    // El log lista los nombres reconocidos: es la forma de distinguir "mi
    // nombre esta en ops.txt pero el servidor no lo leyo" de "el servidor si
    // me tiene como op y el problema es otro".
    std::string names;
    for (const auto& n : ops_) {
        if (!names.empty()) names += ", ";
        names += n;
    }
    utils::MainLogger::get().info("Loaded " + std::to_string(ops_.size()) +
        " operator(s) from ops.txt" + (names.empty() ? std::string() : ": " + names));
}

void Server::saveOps() {
    std::ofstream out(dataPath_ + "/ops.txt", std::ios::trunc);
    if (!out.is_open()) {
        utils::MainLogger::get().warn("Failed to write ops.txt");
        return;
    }
    for (const auto& name : ops_) out << name << "\n";
    out.close();
}

bool Server::isOp(const std::string& name) const {
    if (name.empty()) return false;
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c){ return std::tolower(c); });
    return ops_.find(lower) != ops_.end();
}

void Server::setOp(const std::string& name, bool op) {
    if (name.empty()) return;
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c){ return std::tolower(c); });
    if (op) {
        if (ops_.insert(lower).second) saveOps();
    } else {
        if (ops_.erase(lower) > 0) saveOps();
    }
}

// Helper: ensure "<dataPath>/players/" exists.
static void ensurePlayersDir(const std::string& dataPath) {
    std::string dir = dataPath + "/players";
    // Create if missing (best-effort; ignores EEXIST).
    ::mkdir(dir.c_str(), 0755);
}

// Helper: build a default NBT compound for a new player.
static NbtPtr buildDefaultPlayerNbt(Server* srv, const std::string& name) {
    mcp_tag_t* root = mcp_compound_tag_new_named("");
    mcp_compound_tag_set_long(root, "firstPlayed",
        (int64_t)(mcp_utils_time_ms()));
    mcp_compound_tag_set_long(root, "lastPlayed",
        (int64_t)(mcp_utils_time_ms()));
    mcp_compound_tag_set_short(root, "Health", 20);
    mcp_compound_tag_set_short(root, "MaxHealth", 20);
    mcp_compound_tag_set_short(root, "Hunger", 20);
    mcp_compound_tag_set_short(root, "Air", 300);
    mcp_compound_tag_set_int (root, "playerGameType", srv->gamemode());
    mcp_compound_tag_set_string(root, "NameTag", name.c_str());
    mcp_compound_tag_set_string(root, "Level", "world");
    if (srv->getDefaultWorld()) {
        auto sp = srv->getDefaultWorld()->spawn();
        mcp_compound_tag_set_int(root, "SpawnX", (int32_t)sp.x);
        mcp_compound_tag_set_int(root, "SpawnY", (int32_t)sp.y);
        mcp_compound_tag_set_int(root, "SpawnZ", (int32_t)sp.z);
    }
    return NbtPtr(root, [](mcp_tag_t* t){ if (t) mcp_tag_destroy(t); });
}

NbtPtr Server::getOfflinePlayerData(const std::string& name) {
    ensurePlayersDir(dataPath_);
    std::string path = dataPath_ + "/players/" + name + ".dat";
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) return buildDefaultPlayerNbt(this, name);

    std::vector<uint8_t> buf((std::istreambuf_iterator<char>(in)),
                              std::istreambuf_iterator<char>());
    if (buf.empty()) return buildDefaultPlayerNbt(this, name);

    // Nukkit-style player.dat: GZIP-compressed big-endian NBT.
    // Fall back to plain little-endian for legacy CurseStone files.
    mcp_tag_t* root = nullptr;
    {
        // Try GZIP + big-endian first.
        z_stream zs{};
        if (inflateInit2(&zs, 15 | 16) == Z_OK) {
            zs.next_in  = (Bytef*)buf.data();
            zs.avail_in = (uInt)buf.size();
            std::vector<uint8_t> inflated;
            inflated.reserve(buf.size() * 4);
            uint8_t chunk[65536];
            int rc = Z_OK;
            do {
                zs.next_out  = chunk;
                zs.avail_out = sizeof(chunk);
                rc = inflate(&zs, Z_NO_FLUSH);
                if (rc != Z_OK && rc != Z_STREAM_END) break;
                size_t produced = sizeof(chunk) - zs.avail_out;
                if (produced > 0) inflated.insert(inflated.end(), chunk, chunk + produced);
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
    return NbtPtr(root, [](mcp_tag_t* t){ if (t) mcp_tag_destroy(t); });
}

void Server::saveOfflinePlayerData(const std::string& name, NbtPtr nbt) {
    if (!nbt) return;
    ensurePlayersDir(dataPath_);
    std::string path = dataPath_ + "/players/" + name + ".dat";
    size_t outLen = 0;
    uint8_t* bytes = mcp_nbt_write_bytes(nbt.get(), /*little_endian=*/false, &outLen);
    if (!bytes || outLen == 0) {
        utils::MainLogger::get().warn("Failed to serialize NBT for " + name);
        if (bytes) free(bytes);
        return;
    }
    // GZIP-compress to match Nukkit player.dat layout.
    z_stream zs{};
    if (deflateInit2(&zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED,
                       15 | 16, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
        free(bytes);
        utils::MainLogger::get().warn("Failed to gzip player data for " + name);
        return;
    }
    size_t bound = deflateBound(&zs, (uLong)outLen);
    std::vector<uint8_t> gz(bound);
    zs.next_in   = (Bytef*)bytes;
    zs.avail_in  = (uInt)outLen;
    zs.next_out  = (Bytef*)gz.data();
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

// ============================================================
// Nukkit-compat method implementations
// ============================================================
Player* Server::getPlayerExact(const std::string& name) const {
    auto snap = playersSnapshot();
    for (const auto& p : snap) {
        if (p) {
            std::string pn = p->getName();
            std::transform(pn.begin(), pn.end(), pn.begin(),
                   [](unsigned char c) { return (char)::tolower(c); });
            std::string nl = name;
            std::transform(nl.begin(), nl.end(), nl.begin(),
                   [](unsigned char c) { return (char)::tolower(c); });
            if (pn == nl) return p.get();
        }
    }
    return nullptr;
}

std::vector<Player*> Server::matchPlayer(const std::string& partialName) const {
    std::vector<Player*> result;
    std::string lower = partialName;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return (char)::tolower(c); });
    for (const auto& p : playersSnapshot()) {
        if (!p) continue;
        std::string pn = p->getName();
        std::transform(pn.begin(), pn.end(), pn.begin(),
                   [](unsigned char c) { return (char)::tolower(c); });
        if (pn.find(lower) != std::string::npos) {
            result.push_back(p.get());
        }
    }
    return result;
}

// --- Nukkit-compat: World management ---
World* Server::getWorldByName(const std::string& name) const {
    if (world_ && world_->name() == name) return world_.get();
    return nullptr;
}

void Server::onPlayerLogin(Player* /*player*/) {
    // Nukkit: called when a player successfully logs in.
    // Used to broadcast join message + update player list.
    // We handle this in Player::handleLogin already.
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
    (void)key; (void)val;
}

void Server::setPropertyInt(const std::string& key, int val) {
    (void)key; (void)val;
}

void Server::setPropertyBoolean(const std::string& key, bool val) {
    (void)key; (void)val;
}

int Server::broadcast(const std::string& message, const std::string& /*permissions*/) {
    broadcastMessage(message);
    return 0;
}

void Server::broadcastPacketsCallback(const std::vector<uint8_t>& /*data*/,
                                       const std::vector<std::string>& /*identifiers*/) {
    // Stub — would batch-send a pre-encoded packet to specific players.
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
        std::string motd = "MCPE;" + motd_ + ";" +
            std::to_string(PROTOCOL_VERSION) + ";" +
            std::string(MINECRAFT_VERSION) + ";" +
            std::to_string(playersSnapshot().size()) + ";" +
            std::to_string(max_players_);
        // Cast to RakLibInterface to access setServerName
        auto* raklib = dynamic_cast<network::RakLibInterface*>(network_->getRakLib());
        if (raklib) raklib->setServerName(motd);
    }
}

void Server::sendRecipeList(Player* player) {
    // Nukkit: sends the full crafting recipe list to a player.
    // Implementación real: arma un CraftingDataPacket con TODAS las
    // recetas cargadas y lo manda a este jugador (doFirstSpawn ya lo
    // manda por su cuenta; este método queda para re-syncs manuales,
    // igual que el sendRecipeList() de Nukkit).
    if (!player || !crafting_) return;
    auto cd = std::make_shared<network::protocol::CraftingDataPacket>();
    cd->addAllRecipes(*crafting_);
    player->sendPacket(cd);
}

void Server::updatePlayerListData(const std::string& /*uuid*/,
                                    const std::string& /*name*/,
                                    int64_t /*eid*/,
                                    const std::string& /*skin*/) {
    // Nukkit: broadcasts PlayerListPacket ADD to all online players.
    // We handle this in Player::doFirstSpawn already.
}

void Server::removePlayerListData(const std::string& /*uuid*/) {
    // Nukkit: broadcasts PlayerListPacket REMOVE to all online players.
    // We handle this in Player::close already.
}

void Server::sendFullPlayerListData(Player* /*target*/) {
    // Nukkit: sends the full player list to a specific player.
    // We handle this in Player::doFirstSpawn already.
}

} // namespace cursestone
