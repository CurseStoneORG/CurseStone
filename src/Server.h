// SPDX-License-Identifier: LGPL-3.0-or-later
// Server — singleton. Mirrors pocketmine/Server.php.
//
// Responsibilities:
//   1. Boot: load cursestone.yml, init logger, register blocks/items,
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
#include <csignal>

namespace cursestone {
class Player;
class CommandMap;
class CommandReader;
namespace level { class Level; }   // resolved via `using Level = level::Level;` in main.h
namespace network { class Network; }
namespace inventory { class CraftingManager; }
namespace scheduler { class AsyncPool; }
namespace permission { class BanList; }
namespace nbt        { namespace tag { class CompoundTag; } }   // C++ class (matches main.h declaration)
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

    // Registers an external "stop requested" flag for runTickLoop() to
    // poll once per iteration on the MAIN thread. main.cpp points this
    // at a volatile sig_atomic_t that its SIGINT/SIGTERM handler sets.
    //
    // This indirection exists because the signal handler itself must
    // never call shutdown() directly — shutdown() logs (malloc + mutex
    // via the async logger) and spawns a watchdog std::thread
    // (pthread_create), neither of which is async-signal-safe. Doing
    // that inside the handler can deadlock the process if the signal
    // interrupts a thread already holding an internal libc lock (e.g.
    // mid-malloc) — deterministic on Linux/glibc, which is why this
    // only ever showed up there: the server would appear to never
    // finish starting, or /stop would hang forever with the process
    // still alive. Polling a plain flag from ordinary (non-signal)
    // code on the main thread keeps shutdown() itself unchanged and
    // safe to call from here.
    void setExternalStopFlag(const volatile std::sig_atomic_t* flag) {
        external_stop_flag_ = flag;
    }

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

    // ---- World behaviour flags (used by BlockFire propagation) ----
    // Both default to TRUE so the fire-spread + Nether portal logic in
    // BlockFire.cpp / ItemFlintSteel.cpp behaves like vanilla PMMP. They
    // are exposed as getters so a future server.properties can override
    // them without touching call sites.
    // mirrors PMMP Server::isFireSpreadEnabled() / isNetherEnabled()
    bool fireSpreadEnabled() const { return fire_spread_enabled_; }
    bool netherEnabled()     const { return nether_enabled_; }

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

    // --- Nukkit-compat: Level management ---
    Level* getLevel(int levelId) const { return (levelId == 1 && level_) ? level_.get() : nullptr; }
    Level* getLevelByName(const std::string& name) const;
    void setDefaultLevel(std::shared_ptr<Level> l) { level_ = std::move(l); }

    // --- Nukkit-compat: Player management ---
    Player* getPlayerExact(const std::string& name) const;
    std::vector<Player*> matchPlayer(const std::string& partialName) const;
    void onPlayerLogin(Player* player);

    // --- Nukkit-compat: Config accessors ---
    // getConfig / getProperty / getAdvancedConfig — we delegate to
    // server.properties which is already loaded. These are stubs.
    std::string getConfigString(const std::string& key, const std::string& def) const;
    int getConfigInt(const std::string& key, int def) const;
    bool getConfigBool(const std::string& key, bool def) const;
    void setPropertyString(const std::string& key, const std::string& val);
    void setPropertyInt(const std::string& key, int val);
    void setPropertyBoolean(const std::string& key, bool val);

    // --- Nukkit-compat: Broadcasting ---
    int broadcast(const std::string& message, const std::string& permissions);
    void broadcastPacketsCallback(const std::vector<uint8_t>& data,
                                   const std::vector<std::string>& identifiers);

    // --- Nukkit-compat: Ops / Whitelist ---
    void addOp(const std::string& name) { setOp(name, true); }
    void removeOp(const std::string& name) { setOp(name, false); }
    void addWhitelist(const std::string& name);
    void removeWhitelist(const std::string& name);
    void reloadWhitelist();

    // --- Nukkit-compat: Auto-save ---
    bool isAutoSaveEnabled() const { return autosave_ticks_ > 0; }
    void setAutoSave(bool enabled) { autosave_ticks_ = enabled ? 6000 : 0; }
    void doAutoSave() { saveAll(); }

    // --- Nukkit-compat: Offline player ---
    void saveOfflinePlayerData(const std::string& name, NbtPtr nbt, bool async);

    // --- Nukkit-compat: Title tick (updates MOTD/TPS display) ---
    void titleTick();

    // --- Nukkit-compat: Recipe list ---
    void sendRecipeList(Player* player);

    // CraftingManager global (recetas registradas en codigo al
    // arranque). Consumido por Player::handleCraftingEvent y
    // TileFurnace::tick. Puede ser nullptr antes de start().
    inventory::CraftingManager* craftingManager() const { return crafting_.get(); }

    // --- Nukkit-compat: Player list data (PlayerListPacket broadcast) ---
    void updatePlayerListData(const std::string& uuid, const std::string& name,
                               int64_t eid, const std::string& skin = "");
    void removePlayerListData(const std::string& uuid);
    void sendFullPlayerListData(Player* target);

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
    std::atomic<bool> tick_loop_exited_{false};  // set true when runTickLoop returns
    // See setExternalStopFlag() above. Read only from runTickLoop() on
    // the main thread — never dereferenced from signal context here.
    const volatile std::sig_atomic_t* external_stop_flag_ = nullptr;
    int tps_ = 20;

    // Config (mirrors resources/cursestone.yml).
    std::string ip_ = "0.0.0.0";
    uint16_t port_ = 19132;
    std::string motd_ = "CurseStone";
    int max_players_ = 20;
    int view_distance_ = 8;
    int gamemode_ = 0;
    int difficulty_ = 1;
    bool whitelist_ = false;
    bool online_mode_ = false;
    bool advanced_log_ = false;
    // Fire spread + Nether portal behaviour (see fireSpreadEnabled / netherEnabled).
    bool fire_spread_enabled_ = true;
    bool nether_enabled_ = true;
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

} // namespace cursestone
