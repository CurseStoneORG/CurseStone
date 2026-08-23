// SPDX-License-Identifier: LGPL-3.0-or-later
// Server singleton implementation (Phase 21 completion).
#include "Server.h"
#include "Player.h"
#include "utils/MainLogger.hpp"
#include "utils/Config.h"
#include "utils/Utils.h"
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

namespace mcppeserver {

Server::Server() = default;
Server::~Server() = default;

void Server::loadConfig() {
    using utils::Config;
    Config cfg;
    std::string cfg_path = dataPath_ + "/mcppeserver.yml";
    if (!cfg.loadYaml(cfg_path)) {
        utils::MainLogger::get().info("Config not found, using defaults: " + cfg_path);
    }
    ip_            = cfg.getString("server.ip", "0.0.0.0");
    port_          = (uint16_t)cfg.getInt("server.port", 19132);
    motd_          = cfg.getString("server.motd", "MCPPEServer");
    max_players_   = cfg.getInt("server.max-players", 20);
    gamemode_      = cfg.getInt("server.gamemode", 0);
    difficulty_    = cfg.getInt("server.difficulty", 1);
    whitelist_     = cfg.getBool("server.whitelist", false);
    online_mode_   = cfg.getBool("server.online-mode", false);
    advanced_log_  = cfg.getBool("server.advanced-log", true);
    view_distance_ = cfg.getInt("server.view-distance", 8);
    async_workers_  = cfg.getInt("settings.async-workers", 4);
}

void Server::initLogger() {
    utils::MainLogger::get().info(std::string(MCPPESERVER_NAME) +
        " v" + MCPPESERVER_VERSION + " (codename: " + MCPPESERVER_CODENAME + ")");
    utils::MainLogger::get().info("Target: Minecraft PE " + std::string(MINECRAFT_VERSION) +
        ", protocol " + std::to_string(PROTOCOL_VERSION));
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
    level::LevelConfig lc;
    lc.name = "world";
    lc.format = "mcacpp";
    lc.generator = "normal";
    lc.seed = 0;
    lc.spawn_x = 0; lc.spawn_y = 5; lc.spawn_z = 0;
    lc.view_distance = view_distance_;
    level_ = std::make_shared<level::Level>(std::move(lc));
    // Create worlds directory so the server has a visible data folder
    std::string worldsDir = dataPath_ + "/worlds";
    ::mkdir(worldsDir.c_str(), 0755);
    // Create a marker file so the user can see the world exists
    std::string marker = worldsDir + "/world.txt";
    FILE* f = fopen(marker.c_str(), "w");
    if (f) { fprintf(f, "World: world\nFormat: mcacpp\nSpawn: 0, 66, 0\n"); fclose(f); }
    utils::MainLogger::get().info("Default level 'world' ready");
    utils::MainLogger::get().info("Worlds directory: " + worldsDir);
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
    dataPath_ = dataPath;
    running_.store(true, std::memory_order_relaxed);

    loadConfig();
    loadOps();
    initLogger();
    registerBlocksAndItems();
    command_map_ = std::make_shared<SimpleCommandMap>();
    registerDefaultCommands(*command_map_);
    crafting_ = std::make_shared<inventory::CraftingManager>();
    crafting_->loadFromJson(dataPath_ + "/resources/recipes.json");
    async_pool_ = std::make_shared<scheduler::AsyncPool>(async_workers_);
    openNetwork();
    loadDefaultLevel();
    startCommandReader();

    utils::MainLogger::get().info("Server ready. Tick loop starting...");
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
}

void Server::tick() {
    ++currentTick_;
    if (scheduler::ServerScheduler::get().size() > 0)
        scheduler::ServerScheduler::get().tick(currentTick_);
    if (level_) level_->tick(currentTick_);
    // Update each player (movement, chunk streaming, etc.)
    for (auto& p : players_) {
        if (p) p->onUpdate(currentTick_);
    }
    // Network tick (updates avg packet rate, etc.)
    if (network_) network_->tick();
}

void Server::shutdown() {
    running_.store(false, std::memory_order_relaxed);
    utils::MainLogger::get().info("Server shutting down...");
    if (network_) network_->tick();   // graceful close
    if (level_) level_->save();
    if (console_reader_) console_reader_->stop();
    utils::MainLogger::get().info("Shutdown complete.");
}

Player* Server::getPlayer(const std::string& name) const {
    for (const auto& p : players_)
        if (p && p->getName() == name) return p.get();
    return nullptr;
}
void Server::addPlayer(std::shared_ptr<Player> p) {
    players_.push_back(std::move(p));
}
void Server::removePlayer(Player* p) {
    players_.erase(std::remove_if(players_.begin(), players_.end(),
        [p](const std::shared_ptr<Player>& q) { return q.get() == p; }), players_.end());
}
void Server::broadcastMessage(const std::string& msg) {
    for (auto& p : players_) p->sendMessage(msg);
}
void Server::broadcastPacket(void* /*p*/) {
    // send to each player's source interface
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

    mcp_bstream_t bs;
    mcp_bstream_init_read(&bs, buf.data(), buf.size());
    mcp_tag_t* root = mcp_nbt_read_root(&bs, /*little_endian=*/true);
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
    uint8_t* bytes = mcp_nbt_write_bytes(nbt.get(), /*little_endian=*/true, &outLen);
    if (!bytes || outLen == 0) {
        utils::MainLogger::get().warn("Failed to serialize NBT for " + name);
        if (bytes) free(bytes);
        return;
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        utils::MainLogger::get().warn("Failed to write player file: " + path);
        free(bytes);
        return;
    }
    out.write(reinterpret_cast<const char*>(bytes), (std::streamsize)outLen);
    out.close();
    free(bytes);
}

} // namespace mcppeserver
