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
// Server singleton for startup, ticking, and shutdown.
//
// Responsibilities:
// - Initialize configuration, logging, registries, commands, worlds, and networking.
// - Run the main tick loop for the scheduler, network, worlds, players, and time.
// - Disconnect players, save data, and close networking during shutdown.
//
// Player management:
// - players() returns the online player list.
// - broadcastMessage() sends a message to all players.
// - removePlayer() unregisters a disconnected player.
//
// Commands are dispatched from the console or player chat.
#pragma once
#include "CurseStone.h"
#include "nbt/tag/Tag.h" // cursestone::nbt::Tag
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
namespace world {
class World;
} // World alias is defined in CurseStone.h.
namespace network {
class Network;
}
namespace inventory {
class CraftingManager;
}
namespace scheduler {
class AsyncPool;
}
namespace permission {
class BanList;
}
namespace nbt {
namespace tag {
class CompoundTag;
}
} // NBT class declaration.
namespace block {
void register_all_blocks();
}
namespace item {
class Item;
}

// Shared owner for an NBT root tag; releases it when the reference count reaches zero.
using NbtPtr = std::shared_ptr<cursestone::nbt::Tag>;

// Adopt a raw tag from a parse result; destroys it on last reference.
inline NbtPtr adoptNbt(cursestone::nbt::Tag* t) {
    return NbtPtr(t, [](cursestone::nbt::Tag* p) {
        if (p)
            delete p;
    });
}

/**
 * @brief Server singleton for startup, ticking, and shutdown.
 */
class Server {
  public:
    static Server& getInstance() {
        static Server instance;
        return instance;
    }

    /** @brief Starts the server. Called from CurseStone.cpp. */
    void start(const std::string& dataPath);

    /** @brief Runs the main tick loop until shutdown. */
    void runTickLoop();

    /** @brief Advances one tick across the scheduler, network, world, and players. */
    void tick();

    /** @brief Disconnects players, saves worlds, and closes networking. */
    void shutdown();

    /**
     * Registers a stop flag for runTickLoop() to poll on the main thread.
     * The caller must keep the flag alive until the tick loop exits.
     *
     * Signal handlers must not call shutdown() directly because logging,
     * allocation, mutexes, and thread creation are not async-signal-safe.
     *
     * @param flag Pointer to a volatile sig_atomic_t owned by the caller.
     */
    void setExternalStopFlag(const volatile std::sig_atomic_t* flag) {
        external_stop_flag_ = flag;
    }

    bool isRunning() const {
        return running_.load(std::memory_order_relaxed);
    }
    const std::string& getDataPath() const {
        return dataPath_;
    }
    uint64_t getCurrentTick() const {
        return currentTick_;
    }
    int getTPS() const {
        return tps_;
    }

    // Online player list.
    std::vector<std::shared_ptr<Player>>& players() {
        return players_;
    }
    const std::vector<std::shared_ptr<Player>>& players() const {
        return players_;
    }

    /** @brief Returns a thread-safe snapshot of the online player list. */
    std::vector<std::shared_ptr<Player>> playersSnapshot() const;
    Player* getPlayer(const std::string& name) const;
    void addPlayer(std::shared_ptr<Player> p);
    void removePlayer(Player* p);

    /** @brief Allocates a unique runtime entity ID. */
    int64_t allocateEid();

    /** @brief Saves all players and loaded worlds. */
    void saveAll();

    // Broadcast helpers.
    void broadcastMessage(const std::string& msg);

    // World, scheduler, and network accessors.
    World* getDefaultWorld() const {
        return world_.get();
    }
    scheduler::AsyncPool* getAsyncPool() const {
        return async_pool_.get();
    }
    CommandMap* getCommandMap() const {
        return command_map_.get();
    }
    network::Network* getNetwork() const {
        return network_.get();
    }

    // Multiworld management.
    // The first loaded world is the default world; additional worlds can be loaded by plugins.

    /// @brief Loads a world or returns the existing world with the given name.
    /// @return A borrowed world pointer, or nullptr on failure.
    World* loadWorld(const std::string& name, const std::string& generator);

    /// @brief Unloads a world after moving its players to the default world and saving its chunks.
    bool unloadWorld(const std::string& name);

    /// @brief Returns borrowed pointers to all loaded worlds.
    std::vector<World*> getWorlds() const;

    /// @brief Returns shared ownership of the named world.
    std::shared_ptr<World> getWorldShared(const std::string& name) const;

    // Configuration accessors.
    int maxPlayers() const {
        return max_players_;
    }
    int viewDistance() const {
        return view_distance_;
    }
    int gamemode() const {
        return gamemode_;
    }
    int difficulty() const {
        return difficulty_;
    }
    bool whitelistEnabled() const {
        return whitelist_;
    }
    bool onlineMode() const {
        return online_mode_;
    }
    // The ONE switch that gates every debug/diagnostic line in the
    // console: the RakLib [NET] traces, packet hexdumps, verbose gameplay
    // traces and MainLogger::debug() output. Driven by the
    // `settings.debug-mode` key in cursestone.yml (default false) and
    // mirrored into the C-shared flag (utils/DebugFlag.h) so the RakLib
    // thread and the async logger see the same value.
    bool debugMode() const {
        return debug_mode_;
    }
    const std::string& ip() const {
        return ip_;
    }
    uint16_t port() const {
        return port_;
    }
    std::string getMotd() const {
        return motd_;
    }

    // World behavior flags used by fire and Nether portal mechanics.
    bool fireSpreadEnabled() const {
        return fire_spread_enabled_;
    }
    bool netherEnabled() const {
        return nether_enabled_;
    }

    // Gameplay rules from cursestone.yml.
    bool keepInventory() const {
        return keep_inventory_;
    }
    bool bedSpawnEnabled() const {
        return bed_spawn_enabled_;
    }

    // Ban lists.
    permission::BanList& getNameBans();
    permission::BanList& getIPBans();

    // Operator list stored in <dataPath>/ops.txt.
    bool isOp(const std::string& name) const;
    void setOp(const std::string& name, bool op);

    // Offline player data stored as NBT.
    NbtPtr getOfflinePlayerData(const std::string& name);
    void saveOfflinePlayerData(const std::string& name, NbtPtr nbt);

    // Nukkit-compatible world management.
    // World IDs are 1-based; ID 1 represents the default world.
    World* getWorld(int worldId) const;
    World* getWorldByName(const std::string& name) const;
    void setDefaultWorld(std::shared_ptr<World> l) {
        world_ = std::move(l);
    }

    // Nukkit-compatible player management.
    Player* getPlayerExact(const std::string& name) const;
    std::vector<Player*> matchPlayer(const std::string& partialName) const;
    void onPlayerLogin(Player* player);

    // Nukkit-compatible configuration accessors.
    // These methods currently delegate to server.properties or act as stubs.
    std::string getConfigString(const std::string& key, const std::string& def) const;
    int getConfigInt(const std::string& key, int def) const;
    bool getConfigBool(const std::string& key, bool def) const;
    void setPropertyString(const std::string& key, const std::string& val);
    void setPropertyInt(const std::string& key, int val);
    void setPropertyBoolean(const std::string& key, bool val);

    // Nukkit-compatible broadcasting.
    int broadcast(const std::string& message, const std::string& permissions);

    // Nukkit-compatible operators and whitelist.
    void addOp(const std::string& name) {
        setOp(name, true);
    }
    void removeOp(const std::string& name) {
        setOp(name, false);
    }

    // Nukkit-compatible autosave.
    bool isAutoSaveEnabled() const {
        return autosave_ticks_ > 0;
    }
    void setAutoSave(bool enabled) {
        autosave_ticks_ = enabled ? 6000 : 0;
    }
    void doAutoSave() {
        saveAll();
    }

    // Incremental autosave spreads chunk writes across multiple ticks.
    // This avoids blocking the main thread with a full world save.
    // Per-tick work is bounded by kAutosaveChunksPerTick and kAutosaveMsPerTick.
    void beginAutosaveDrain();
    void tickAutosaveDrain();

    // Nukkit-compatible offline player data.
    void saveOfflinePlayerData(const std::string& name, NbtPtr nbt, bool async);

    // Nukkit-compatible title updates.
    void titleTick();

    // Nukkit-compatible recipe list.
    void sendRecipeList(Player* player);

    // Shared crafting manager used by player crafting and furnace updates.
    // May be nullptr before startup.
    inventory::CraftingManager* craftingManager() const {
        return crafting_.get();
    }

    // Nukkit-compatible player list data.
    void updatePlayerListData(const std::string& uuid, const std::string& name, int64_t eid,
                              const std::string& skin = "");
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
    void loadDefaultWorld();
    void startCommandReader();
    void loadOps();
    void saveOps();

    std::unordered_set<std::string> ops_;

    std::string dataPath_;
    std::atomic<bool> running_{false};
    uint64_t currentTick_{0};
    std::atomic<bool> tick_loop_exited_{false}; // Set when runTickLoop() exits.
    // Read only by runTickLoop() on the main thread.
    const volatile std::sig_atomic_t* external_stop_flag_ = nullptr;
    int tps_ = 20;

    // Configuration from server.properties and cursestone.yml.
    std::string ip_ = "0.0.0.0";
    uint16_t port_ = 19132;
    std::string motd_ = "CurseStone";
    int max_players_ = 20;
    int view_distance_ = 8;
    int gamemode_ = 0;
    int difficulty_ = 1;
    bool whitelist_ = false;
    bool online_mode_ = false;
    // Master switch for every debug/diagnostic console line
    // (settings.debug-mode in cursestone.yml, default false).
    bool debug_mode_ = false;

    // Fire spread and Nether portal behavior.
    bool fire_spread_enabled_ = true;
    bool nether_enabled_ = true;

    // Gameplay rules.
    bool keep_inventory_ = false;
    bool bed_spawn_enabled_ = true;
    int async_workers_ = 4;
    int autosave_ticks_ = 6000; // Five minutes at 20 TPS.

    // Incremental autosave state.
    bool autosave_draining_ = false;
    uint64_t autosave_drain_start_ms_ = 0;
    size_t autosave_drain_written_ = 0;
    uint64_t autosave_drain_ticks_ = 0;
    uint64_t autosave_drain_max_tick_ms_ = 0;
    int autosave_drain_idle_ticks_ = 0;

    // Subsystems initialized during startup.
    std::shared_ptr<network::Network> network_;
    std::shared_ptr<World> world_;               // Default world and spawn.
    std::vector<std::shared_ptr<World>> worlds_; // All loaded worlds; worlds_[0] is the default.
    std::shared_ptr<CommandMap> command_map_;
    std::shared_ptr<inventory::CraftingManager> crafting_;
    std::shared_ptr<scheduler::AsyncPool> async_pool_;
    std::unique_ptr<CommandReader> console_reader_;
    std::vector<std::shared_ptr<Player>> players_;
    mutable std::mutex players_mtx_; // Protects players_.

    // Monotonic runtime entity ID allocator; IDs 0 and 1 are reserved.
    std::atomic<int64_t> next_eid_{2};

    // Lazily initialized ban lists.
    std::unique_ptr<permission::BanList> name_bans_;
    std::unique_ptr<permission::BanList> ip_bans_;
};

} // namespace cursestone