// SPDX-License-Identifier: LGPL-3.0-or-later
// Server singleton implementation (Phase 21 completion).
#include "Server.h"
#include "Player.h"
#include "utils/MainLogger.h"
#include "utils/MainLogger.hpp"
#include "utils/Config.h"
#include "utils/Utils.h"

#include <climits>
#include <cstring>
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
        bool v = cfg.getBool(ini_key, def);
        if (v != def) return v;   // exact match needed because INI may be true/false
        return cfg.getBool(yaml_key, def);
    };

    ip_            = get_str("server.ip", "server-ip", "0.0.0.0");
    port_          = (uint16_t)get_int("server.port", "server-port", 19132);
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
        auto start_ts = std::chrono::steady_clock::now();
        tick();
        auto elapsed = std::chrono::steady_clock::now() - start_ts;
        auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
        if (elapsed_ms < TICK_MS) {
            std::this_thread::sleep_for(std::chrono::milliseconds(TICK_MS - elapsed_ms));
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
    if (scheduler::ServerScheduler::get().size() > 0)
        scheduler::ServerScheduler::get().tick(currentTick_);
    if (level_) level_->tick(currentTick_);
    // Update each player (movement, chunk streaming, etc.) via snapshot
    // so we don't race with add/remove on the network thread.
    auto snap = playersSnapshot();
    for (auto& p : snap) {
        if (p) p->onUpdate(currentTick_);
    }
    // Network tick (updates avg packet rate, etc.)
    if (network_) network_->tick();

    // Auto-save every `autosave_ticks_` ticks (default 6000 = 5 min at 20 TPS).
    // Mirrors PMMP `Server::autoSave()` which runs every 6000 ticks.
    if (autosave_ticks_ > 0 && currentTick_ % autosave_ticks_ == 0) {
        saveAll();
    }
}

void Server::shutdown() {
    // Signal the tick loop to exit. The actual cleanup (saveAll,
    // console_reader stop) is deferred to AFTER the tick loop exits
    // in runTickLoop(), because /stop may be dispatched from the
    // CommandReader thread. Doing saveAll() here would race with the
    // main thread's destructors (mutexes, level_, players_) — causing
    // "pthread_mutex_lock called on a destroyed mutex" and lost chunks.
    running_.store(false, std::memory_order_relaxed);
    utils::MainLogger::get().info("Server shutting down...");
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

} // namespace atomikpe
