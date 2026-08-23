// SPDX-License-Identifier: LGPL-3.0-or-later
// Server — singleton. Mirrors pocketmine/Server.php.
//
// Responsibilities:
//   1. Boot: load cenisyspe.yml, init logger, register blocks/items,
//      create CommandMap + register default commands, create the
//      default Level (overworld), open the RakLibInterface.
//   2. Tick (every 50ms): advance scheduler, tick level(s), tick
//      players, drain RakLib→main FIFO, advance time.
//   3. Shutdown: disconnect all players, save level(s), close network.
//
// Player roster + broadcasting:
//   - players() returns the list of online Players (read-only).
//   - broadcastMessage() sends a TextPacket to every player.
//   - removePlayer() unregisters a closed Player.
//
// Command processing:
//   - console input via CommandReader thread → SimpleCommandMap::dispatch.
//   - player chat /slash via Player::handleChat → SimpleCommandMap::dispatch.
#pragma once
#include "main.h"
#include "nbt/tag/Tag.h"               // mcp_tag_t / ::mcp_tag
#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <cstdint>
#include <unordered_set>

namespace cenisyspe {
class Player;
class CommandMap;
class CommandReader;
namespace level { class Level; }   // resolved via `using Level = level::Level;` in main.h
namespace network { class Network; }
namespace inventory { class CraftingManager; }
namespace scheduler { class AsyncPool; }
namespace permission { class BanList; }
namespace nbt        { namespace tag { struct CompoundTag; } }   // C struct
namespace block    { void register_all_blocks(); }
namespace item     { class Item; }

// Owning shared pointer for an NBT root tag (mcp_tag_t*). Frees via
// mcp_tag_destroy when the refcount drops to zero. Defined in Server.cpp.
using NbtPtr = std::shared_ptr<::mcp_tag>;

class Server {
public:
    static Server& getInstance() {
        static Server instance;
        return instance;
    }

    // Boot the server. Called from main.cpp.
    void start(const std::string& dataPath);

    // Run the main tick loop (blocks until shutdown()).
    void runTickLoop();

    // Advance one tick. Dispatches to scheduler, network, level, players.
    void tick();

    // Cleanly shut down: disconnect players, save level, close sockets.
    void shutdown();

    bool isRunning() const { return running_.load(std::memory_order_relaxed); }
    const std::string& getDataPath() const { return dataPath_; }
    uint64_t getCurrentTick() const { return currentTick_; }
    int getTPS() const { return tps_; }

    // Player roster.
    std::vector<std::shared_ptr<Player>>& players() { return players_; }
    const std::vector<std::shared_ptr<Player>>& players() const { return players_; }
    // Thread-safe snapshot of the player list. Callers iterate this copy
    // WITHOUT holding the mutex, so add/remove from other threads does not
    // invalidate iterators (fixes the segfault on concurrent broadcasts).
    std::vector<std::shared_ptr<Player>> playersSnapshot() const;
    Player* getPlayer(const std::string& name) const;
    void addPlayer(std::shared_ptr<Player> p);
    void removePlayer(Player* p);

    // Allocate a fresh unique runtime entity ID. Used for spawning item
    // drops and other world entities (mirrors PMMP Entity::$entityCount).
    int64_t allocateEid();

    // Save ALL players + the default level. Called periodically from the
    // tick loop (auto-save) and on shutdown.
    void saveAll();

    // Broadcast helpers.
    void broadcastMessage(const std::string& msg);
    void broadcastPacket(/* DataPacket */ void* p);

    // Level + scheduler + network accessors.
    Level* getDefaultLevel() const { return level_.get(); }
    scheduler::AsyncPool* getAsyncPool() const { return async_pool_.get(); }
    CommandMap* getCommandMap() const { return command_map_.get(); }
    network::Network* getNetwork() const { return network_.get(); }

    // Settings convenience accessors.
    int maxPlayers() const { return max_players_; }
    int viewDistance() const { return view_distance_; }
    int gamemode() const { return gamemode_; }
    int difficulty() const { return difficulty_; }
    bool whitelistEnabled() const { return whitelist_; }
    bool onlineMode() const { return online_mode_; }
    bool advancedLog() const { return advanced_log_; }
    const std::string& ip() const { return ip_; }
    uint16_t port() const { return port_; }
    std::string getMotd() const { return motd_; }

    // Ban lists (lazily-initialized singletons).
    permission::BanList& getNameBans();
    permission::BanList& getIPBans();

    // Operator list (backed by <dataPath>/ops.txt).
    bool isOp(const std::string& name) const;
    void setOp(const std::string& name, bool op);

    // Offline player data: load/save NBT blobs from "<dataPath>/players/<name>.dat".
    // Returns nullptr if the player has never played before.
    NbtPtr getOfflinePlayerData(const std::string& name);
    void saveOfflinePlayerData(const std::string& name, NbtPtr nbt);

private:
    Server();
    ~Server();
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    void loadConfig();
    void initLogger();
    void registerBlocksAndItems();
    void openNetwork();
    void loadDefaultLevel();
    void startCommandReader();
    void loadOps();
    void saveOps();

    std::unordered_set<std::string> ops_;

    std::string dataPath_;
    std::atomic<bool> running_{false};
    uint64_t currentTick_{0};
    int tps_ = 20;

    // Config (mirrors resources/cenisyspe.yml).
    std::string ip_ = "0.0.0.0";
    uint16_t port_ = 19132;
    std::string motd_ = "CenisysPE";
    int max_players_ = 20;
    int view_distance_ = 8;
    int gamemode_ = 0;
    int difficulty_ = 1;
    bool whitelist_ = false;
    bool online_mode_ = false;
    bool advanced_log_ = false;
    int async_workers_ = 4;
    int autosave_ticks_ = 6000;   // 5 minutes at 20 TPS (PMMP default)

    // Subsystems (lazily populated in start()).
    std::shared_ptr<network::Network> network_;
    std::shared_ptr<Level> level_;
    std::shared_ptr<CommandMap> command_map_;
    std::shared_ptr<inventory::CraftingManager> crafting_;
    std::shared_ptr<scheduler::AsyncPool> async_pool_;
    std::unique_ptr<CommandReader> console_reader_;
    std::vector<std::shared_ptr<Player>> players_;
    mutable std::mutex players_mtx_;             // guards players_

    // Runtime entity-ID allocator (atomic, monotonic from 2 — 0 and 1 are
    // reserved for "no entity" and "host player" respectively).
    std::atomic<int64_t> next_eid_{2};

    // Lazy-init ban lists.
    std::unique_ptr<permission::BanList> name_bans_;
    std::unique_ptr<permission::BanList> ip_bans_;
};

} // namespace cenisyspe
