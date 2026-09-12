// SPDX-License-Identifier: LGPL-3.0-or-later
// Server singleton implementation (Phase 21 completion).
#include "Server.h"
#include "workbench/WorkbenchManager.h"
#include "Player.h"
#include "utils/MainLogger.h"
#include "utils/MainLogger.hpp"
#include "utils/Config.h"
#include "utils/Utils.h"

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
#include "scheduler/AsyncPool.h"
#include "scheduler/ServerScheduler.h"
#include "level/Level.h"
#include "level/ChunkManager.h"
#include "level/format/LevelProviderManager.h"
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

#include <iostream>
#include <fstream>
#include <chrono>
#include <thread>
#include <algorithm>
#include <sys/stat.h>
#include <sys/types.h>
#include <cctype>

namespace atomikpe {

Server::Server() = default;
Server::~Server() = default;

void Server::loadConfig() {
    using utils::Config;
    Config cfg;

    // First try PMMP-style server.properties (key=value INI file).
    // If it doesn't exist, fall back to atomikpe.yml.
    std::string props_path = dataPath_ + "/server.properties";
    std::string yaml_path  = dataPath_ + "/atomikpe.yml";
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

    // ROOT CAUSE FIX ("works on local/Android host, but the public
    // Linux/Pterodactyl server never sees a single incoming packet —
    // not even the raw RakNet CLIENT_CONNECT, confirmed by diagnostic
    // logging added directly in SessionManager.c never firing"):
    //
    // Pterodactyl (Wings) assigns a public port per server instance —
    // shown in the panel as e.g. "gold.magmanode.com:27526" — and only
    // forwards UDP traffic on THAT port into the container. It does
    // this independently of whatever is written in server.properties,
    // which the panel doesn't control or rewrite once the egg's default
    // file has been copied into place on first boot. This binary's
    // bundled resources/server.properties hardcodes server-port=19132
    // (the standard Bedrock port), so on a fresh install the process
    // binds 19132 inside the container while Wings is only forwarding
    // 27526 from the outside — every client packet arrives at Wings,
    // gets forwarded to a port nothing is listening on, and is dropped
    // before it ever reaches this process. That's invisible from
    // inside the server (no failed bind, no error — the bind on 19132
    // succeeds just fine) and only shows up as "clients can't connect
    // to the public address" while a direct/local connection (which
    // isn't going through that port-forward at all) works normally —
    // exactly the reported pattern.
    //
    // Pterodactyl eggs are expected to read the actual assigned port
    // from the SERVER_PORT (and SERVER_IP, less commonly overridden)
    // environment variables it injects into the container at startup,
    // precisely because the assigned port is chosen per-installation
    // and can't be known in advance by any bundled config file. Give
    // those precedence over server.properties/atomikpe.yml whenever
    // they're present — this matches what every other Pterodactyl-
    // compatible server software does, and means a stale or
    // never-updated server-port in the properties file can no longer
    // silently strand the process on the wrong port.
    if (const char* envPort = std::getenv("SERVER_PORT")) {
        if (envPort[0] != '\0') {
            try {
                int p = std::stoi(envPort);
                if (p > 0 && p <= 65535) {
                    if ((uint16_t)p != port_) {
                        utils::MainLogger::get().info(
                            "SERVER_PORT env var (" + std::string(envPort) +
                            ") overrides server-port from config (" +
                            std::to_string(port_) + ")");
                    }
                    port_ = (uint16_t)p;
                }
            } catch (...) {
                utils::MainLogger::get().warn(
                    "Ignoring malformed SERVER_PORT env var: " + std::string(envPort));
            }
        }
    }
    if (const char* envIp = std::getenv("SERVER_IP")) {
        if (envIp[0] != '\0') {
            ip_ = envIp;
        }
    }
    motd_          = get_str("server.motd", "motd", "AtomikPE");
    max_players_   = (int)get_int("server.max-players", "max-players", 20);
    gamemode_      = (int)get_int("server.gamemode", "gamemode", 0);
    difficulty_    = (int)get_int("server.difficulty", "difficulty", 1);
    whitelist_     = get_bool("server.whitelist", "white-list", false);
    online_mode_   = get_bool("server.online-mode", "online-mode", false);
    advanced_log_  = get_bool("server.advanced-log", "advanced-log", true);
    view_distance_ = (int)get_int("server.view-distance", "view-distance", 8);
    async_workers_ = (int)get_int("settings.async-workers", "async-workers", 4);
    autosave_ticks_ = (int)get_int("server.autosave-ticks", "autosave-ticks", 6000);

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
            cfg.getString("level-format", "anvil"));
    }
}

void Server::initLogger() {
    // Open server.log in the data directory so all log lines are written
    // to disk in real time (PMMP-equivalent: pocketmine.log).
    std::string log_path = dataPath_ + "/server.log";
    mcp_main_logger_init(log_path.c_str(), /*colored=*/true);
    utils::MainLogger::get().info(std::string(ATOMIKPE_NAME) +
        " v" + ATOMIKPE_VERSION + " (codename: " + ATOMIKPE_CODENAME + ")");
    utils::MainLogger::get().info("Target: Minecraft PE " + std::string(MINECRAFT_VERSION) +
        ", protocol " + std::to_string(PROTOCOL_VERSION));
    utils::MainLogger::get().info("Log file: " + log_path);
}

void Server::registerBlocksAndItems() {
    block::register_all_blocks();
    item::Item::registerAll();
    utils::MainLogger::get().info("Registered blocks + items");
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

void Server::loadDefaultLevel() {
    // Load level name/format/generator from server.properties or atomikpe.yml.
    using utils::Config;
    Config cfg;
    cfg.loadIni(dataPath_ + "/server.properties");
    cfg.loadYaml(dataPath_ + "/atomikpe.yml");

    auto get_str = [&](const char* yaml_key, const char* ini_key,
                       const char* def) -> std::string {
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

    level::LevelConfig lc;
    lc.name      = get_str("level.name", "level-name", "world");
    lc.format    = get_str("level.format", "level-format", "anvil");
    lc.generator = get_str("level.generator", "generator", "normal");
    int64_t seed = get_int("level.seed", "level-seed", 0);
    lc.seed = (seed != 0) ? (int64_t)seed : (int64_t)mcp_utils_time_ms();
    lc.spawn_x = 0; lc.spawn_y = 64; lc.spawn_z = 0;
    lc.view_distance = view_distance_;
    lc.dataPath = dataPath_;   // so Level can compute absolute world paths

    // Create the world directory structure BEFORE constructing the Level,
    // so the LevelProvider can open the region/ subdirectory on init().
    // Mirrors Nukkit's Anvil.generate(): worlds/<name>/ + worlds/<name>/region/
    std::string worldsDir = dataPath_ + "/worlds";
    std::string worldDir  = worldsDir + "/" + lc.name;
    std::string regionDir = worldDir + "/region";
    ::mkdir(worldsDir.c_str(), 0755);
    ::mkdir(worldDir.c_str(), 0755);
    ::mkdir(regionDir.c_str(), 0755);

    // Construct the Level — this instantiates the LevelProvider (Anvil by
    // default) and calls provider->init(worldDir). The provider creates the
    // region/ subdir if missing and reads level.dat if it exists.
    level_ = std::make_shared<level::Level>(std::move(lc));

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
    crafting_->loadFromJson(dataPath_ + "/resources/recipes.json");
    async_pool_ = std::make_shared<scheduler::AsyncPool>(async_workers_);
    utils::MainLogger::get().info("Opening network interface...");
    openNetwork();
    utils::MainLogger::get().info("Loading default level...");
    loadDefaultLevel();
    utils::MainLogger::get().info("Starting console command reader...");
    startCommandReader();

    // Compute total boot time.
    auto boot_end = std::chrono::steady_clock::now();
    auto boot_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        boot_end - boot_start).count();
    double boot_sec = (double)boot_ms / 1000.0;
    utils::MainLogger::get().info("Server ready. Tick loop starting...");
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
    while (running_.load(std::memory_order_relaxed)) {
        // Poll the external stop flag (set by main.cpp's SIGINT/SIGTERM
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
        if (elapsed_ms < ATOMIK_TICK_MS) {
            std::this_thread::sleep_for(std::chrono::milliseconds(ATOMIK_TICK_MS - elapsed_ms));
        }
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

    // ====================================================================
    // Cleanup — runs on the MAIN thread, AFTER the tick loop exits.
    // This is critical: /stop is dispatched from the CommandReader
    // thread, so doing saveAll() inside Server::shutdown() would race
    // with the main thread destroying Server members (mutexes, level_,
    // players_). By deferring all cleanup to here, we guarantee that
    // saveAll() runs single-threaded before any destructor fires.
    // ====================================================================
    if (network_) network_->tick();   // graceful RakLib close
    saveAll();
    if (console_reader_) console_reader_->stop();
    utils::MainLogger::get().info("Shutdown complete.");
}

void Server::tick() {
    ++currentTick_;

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

    // --- Phase: level ---
    t0 = std::chrono::steady_clock::now();
    if (level_) level_->tick(currentTick_);
    t1 = std::chrono::steady_clock::now();
    WorkbenchManager::get().onTickPhase("level",
        (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count());

    // --- Phase: chunk unload sweep ---
    // Runs every 100 ticks (~5s at 20 TPS), not every tick — this walks
    // every loaded chunk and can trigger disk saves, so it's priced like
    // an occasional maintenance pass, not per-tick work. Frees chunks
    // (and any tile-entities inside them — see Level::evictTilesForChunk)
    // that no connected player can currently see. Before this existed,
    // ChunkManager only ever grew: getChunk() added chunks on demand but
    // nothing ever called the equivalent of unloadChunk(), so a server
    // left running would accumulate every chunk any player had ever
    // walked near, for the lifetime of the process.
    if (level_ && currentTick_ % 100 == 0) {
        t0 = std::chrono::steady_clock::now();
        std::vector<level::Level::ChunkViewer> viewers;
        {
            std::lock_guard<std::mutex> lk(players_mtx_);
            viewers.reserve(players_.size());
            for (auto& p : players_) {
                if (!p) continue;
                // FIX: buildChunkSpiral() now takes a true per-axis
                // radius (see Player.cpp), so viewDistance() itself is
                // the real radius a player can see — no more /2
                // compensation needed here.
                viewers.push_back({p->chunkX(), p->chunkZ(), p->viewDistance()});
            }
        }
        // BUG FIX (root cause of "chunks never render again after
        // reconnecting"): viewers can be EMPTY during the gap between a
        // player disconnecting and (maybe) someone reconnecting — this
        // sweep runs on a fixed tick interval regardless of player
        // count. With an empty viewer list, unloadDistantChunks() found
        // that NO loaded chunk was "needed" by anyone, and unloaded
        // (synchronously saved + freed) the ENTIRE in-memory world,
        // spawn chunks included. The next player to log in then has to
        // reload/regenerate every chunk around their saved position,
        // including the one they're literally standing in, one chunk
        // per tick — which is exactly the "stuck in an empty chunk,
        // world looks empty" symptom, and only ever on a RECONNECT
        // because the first-ever join never hit an empty-viewers sweep.
        // Skip the sweep entirely when nobody is online; there is
        // nothing to unload for yet, and the world stays warm in memory
        // for whoever logs in next.
        if (!viewers.empty()) {
            int unloaded = level_->unloadDistantChunks(viewers);
            if (unloaded > 0 && advancedLog()) {
                utils::MainLogger::get().info("[Chunks] Unloaded " + std::to_string(unloaded) +
                    " chunk(s) out of view.");
            }
        }
        t1 = std::chrono::steady_clock::now();
        WorkbenchManager::get().onTickPhase("chunk_unload",
            (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count());
    }

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

    // --- Phase: network (second pass — flush outbound) ---
    if (network_) network_->tick();

    // --- Phase: auto-save ---
    // BUG FIX: Instead of saving ALL dirty chunks in one synchronous call
    // (which blocks the main thread for seconds), we save only a FEW chunks
    // per tick. This spreads the I/O load across many ticks and prevents
    // the "Disconnected from server" bug caused by autosave blocking.
    if (autosave_ticks_ > 0 && currentTick_ % autosave_ticks_ == 0) {
        auto as0 = std::chrono::steady_clock::now();
        // Save player data (fast — just NBT write, not chunk I/O)
        auto psnap = playersSnapshot();
        for (auto& p : psnap) {
            if (p) p->saveData();
        }
        // Save level metadata (fast — just level.dat)
        if (level_) level_->save();  // This calls flushDirty() which now
                                      // saves chunks through the provider
        auto as1 = std::chrono::steady_clock::now();
        uint64_t as_ms = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(as1 - as0).count();
        WorkbenchManager::get().onAutosave(as_ms);
        if (as_ms > 100) {
            utils::MainLogger::get().warn("[SAVE] Autosave took " +
                std::to_string(as_ms) + "ms — this may cause disconnects!");
        }
    }

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
    // main thread's destructors (mutexes, level_, players_) — causing
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
    //    marked dirty on the chunks before the level save runs.
    auto snap = playersSnapshot();
    for (auto& p : snap) {
        if (p) p->saveData();
    }
    utils::MainLogger::get().info("[SAVE] " + std::to_string(snap.size()) +
        " player(s) saved.");

    // 2. Save the default level — flushes dirty chunks to .mca files
    //    AND writes level.dat (seed/spawn/time) via the provider.
    if (level_) {
        int saved = level_->save();
        utils::MainLogger::get().info("[SAVE] Level saved (" +
            std::to_string(saved) + " chunks flushed).");
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
    std::ifstream in(dataPath_ + "/ops.txt");
    if (!in.is_open()) return;
    std::string line;
    while (std::getline(in, line)) {
        // Trim trailing whitespace/CR.
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n' ||
               line.back() == ' ' || line.back() == '\t')) line.pop_back();
        if (line.empty()) continue;
        std::string lower = line;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char c){ return std::tolower(c); });
        ops_.insert(lower);
    }
    utils::MainLogger::get().info("Loaded " + std::to_string(ops_.size()) +
        " operator(s) from ops.txt");
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
    if (srv->getDefaultLevel()) {
        auto sp = srv->getDefaultLevel()->spawn();
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
    // Fall back to plain little-endian for legacy AtomikPE files.
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
        // Legacy: try plain little-endian (uncompressed, old AtomikPE).
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
            std::transform(pn.begin(), pn.end(), pn.begin(), ::tolower);
            std::string nl = name;
            std::transform(nl.begin(), nl.end(), nl.begin(), ::tolower);
            if (pn == nl) return p.get();
        }
    }
    return nullptr;
}

std::vector<Player*> Server::matchPlayer(const std::string& partialName) const {
    std::vector<Player*> result;
    std::string lower = partialName;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    for (const auto& p : playersSnapshot()) {
        if (!p) continue;
        std::string pn = p->getName();
        std::transform(pn.begin(), pn.end(), pn.begin(), ::tolower);
        if (pn.find(lower) != std::string::npos) {
            result.push_back(p.get());
        }
    }
    return result;
}

// --- Nukkit-compat: Level management ---
Level* Server::getLevelByName(const std::string& name) const {
    if (level_ && level_->name() == name) return level_.get();
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

void Server::sendRecipeList(Player* /*player*/) {
    // Nukkit: sends the full crafting recipe list to a player.
    // We handle this in Player::doFirstSpawn via CraftingManager.
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

} // namespace atomikpe
