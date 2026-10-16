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

// Represents a world and manages its settings, provider, chunks, and persistence.
#pragma once
#include <string>
#include <memory>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <cstdint>
#include <functional>
#include <mutex>
#include "math/Vector3.h"
#include "physics/Physics.h"
#include "world/ChunkManager.h"
#include "world/LightEngine.h"
#include "world/PortalRegistry.h"
#include "world/format/WorldProvider.h"

namespace cursestone {
class Player;
class Entity;
namespace tile {
class Tile;
}
namespace world::generator {
class Generator;
}
}

namespace cursestone::world {
namespace particle {
class Particle;
}

// Represents a flying or stuck arrow and tracks its server-side physics.
struct ArrowEntity {
    int64_t eid = 0;
    int64_t shooter_eid = -1; // Shooter runtime ID; -1 means none.
    double x = 0.0, y = 0.0, z = 0.0;
    double vx = 0.0, vy = 0.0, vz = 0.0;
    float yaw = 0.0f, pitch = 0.0f;
    // Last movement values broadcast to clients.
    double lx = 0.0, ly = 0.0, lz = 0.0;
    float lyaw = 0.0f, lpitch = 0.0f;
    // Last motion values broadcast to clients.
    double lmx = 0.0, lmy = 0.0, lmz = 0.0;
    uint64_t spawn_tick = 0;
    uint64_t flight_ticks = 0; // Used to prevent early self-hits.
    float damage = 2.0f;       // Default Infinity Arrow damage.
    bool critical = false;     // True for fully charged shots.
    bool stuck = false;        // True after hitting a block.
    bool taken = false;        // True after being claimed for pickup.
};

// Tracks a dropped item, its position, motion, and pickup state.
struct DroppedItem {
    int64_t eid = 0; // Runtime entity ID.
    uint16_t itemId = 0;
    uint8_t count = 1;
    uint16_t damage = 0;
    // Raw little-endian NBT preserves custom item names.
    std::string nbt;
    double x = 0.0, y = 0.0, z = 0.0;
    uint64_t spawn_tick = 0; // Server tick when dropped.
    // Pickup delay in ticks; defaults to 10 (0.5 seconds).
    uint32_t pickup_delay_ticks = 10;
    bool taken = false; // Marked for removal after pickup.

    // Motion is integrated server-side to match the client.
    double vx = 0.0, vy = 0.0, vz = 0.0;
    // True when the last downward movement was blocked.
    bool on_ground = false;
    // Last position sent to clients for movement updates.
    double lx = 0.0, ly = 0.0, lz = 0.0;
};

struct FallingBlockEntity {
    int64_t eid = 0;
    uint8_t blockId = 0;              // Block ID restored on landing.
    uint8_t blockData = 0;            // Block metadata preserved during the fall.
    double x = 0.0, y = 0.0, z = 0.0; // Entity position.
    double vx = 0.0, vy = 0.0, vz = 0.0;
    // Last position broadcast to clients.
    double lx = 0.0, ly = 0.0, lz = 0.0;
    uint64_t spawn_tick = 0;
    uint32_t age = 0; // Ticks lived.
    // Used to calculate anvil fall damage.
    float fall_distance = 0.0f;
    bool taken = false; // Removed on the next cleanup pass.
};

// Defines the vanilla dropped-item pickup volume for MCPE 0.15.10.
// Items are collected when their bounding boxes overlap the player's box grown by one block.
// The shared bounds keep attraction and pickup checks consistent.
constexpr double kItemHalfW = 0.125;      // Half of the item width.
constexpr double kItemHeight = 0.25;      // Item height.
constexpr double kPickupHalfExtent = 1.3; // Player half-width plus pickup range.
constexpr double kPickupBelowFeet = 1.0;  // Pickup range below the feet.
constexpr double kPickupAboveFeet = 2.8;  // Player height plus pickup range.

// Checks strict AABB overlap between an item and the player's pickup volume.
inline bool itemWithinPickupVolume(double ix, double iy, double iz, double px, double py,
                                   double pz) {
    if (!(ix + kItemHalfW > px - kPickupHalfExtent && ix - kItemHalfW < px + kPickupHalfExtent))
        return false;
    if (!(iy + kItemHeight > py - kPickupBelowFeet && iy < py + kPickupAboveFeet))
        return false;
    if (!(iz + kItemHalfW > pz - kPickupHalfExtent && iz - kItemHalfW < pz + kPickupHalfExtent))
        return false;
    return true;
}

inline bool itemWithinPickupVolume(const DroppedItem& di, double px, double py, double pz) {
    return itemWithinPickupVolume(di.x, di.y, di.z, px, py, pz);
}

struct WorldConfig {
    std::string name;
    std::string format = "anvil";     ///< Supported formats: anvil and leveldb.
    std::string generator = "normal"; ///< Supported generators: normal, flat, Nether, void.
    int64_t seed = 0;
    /// @brief World dimension: 0 for surface, 1 for Nether.
    /// Nether generators default to dimension 1 when unset.
    uint8_t dimension = 0;
    int32_t time = 0;
    int32_t spawn_x = 0, spawn_y = 64, spawn_z = 0;
    int32_t view_distance = 8;
    std::string dataPath; // Absolute server data directory.
};

class World {
  public:
    explicit World(WorldConfig cfg);
    ~World();

    const std::string& name() const {
        return cfg_.name;
    }

    int64_t seed() const {
        return cfg_.seed;
    }

    const WorldConfig& config() const {
        return cfg_;
    }

    /// @brief Returns the world dimension: 0 for surface, 1 for Nether.
    uint8_t dimension() const {
        return cfg_.dimension;
    }

    ChunkManager& chunks() {
        return chunks_;
    }

    // Provides access to this world's persistent Nether portal registry.
    PortalRegistry& portals() {
        return portals_;
    }

    // Saves the portal registry immediately after registration.
    void savePortals() {
        portals_.save(portals_path_);
    }

    /// @brief Returns the block ID; unloaded chunks are not generated.
    uint8_t getBlockId(int32_t x, int32_t y, int32_t z);

    // Returns block metadata, or 0 when the chunk is unloaded.
    uint8_t getBlockData(int32_t x, int32_t y, int32_t z);

    void setBlock(int32_t x, int32_t y, int32_t z, uint8_t id, uint8_t data);

    // Returns world-space collision boxes within the requested region.
    std::vector<physics::BlockBox> collisionBoxes(mcp_aabb_t region);

    // Queues lighting updates for newly added chunks.
    void onChunkAdded(int32_t cx, int32_t cz);

    // Updates the furnace's lit state and reports whether it changed.
    bool setFurnaceFlame(int32_t x, int32_t y, int32_t z, bool lit);

    // Checks chunk loading without triggering generation.
    bool isChunkLoaded(int32_t x, int32_t z) {
        return chunks_.getChunkIfLoaded(x >> 4, z >> 4) != nullptr;
    }

    // Returns the world time in ticks.
    int32_t time() const {
        return cfg_.time;
    }

    void setTime(int32_t t) {
        cfg_.time = t;
    }

    void advanceTime(int dt_ticks) {
        cfg_.time = (cfg_.time + dt_ticks) % 24000;
    }

    // ----- Sleep and skip-night system -----
    // Advances the pending sleep countdown and checks whether all players are sleeping.
    static constexpr int32_t TIME_SUNSET = 12000;
    static constexpr int32_t TIME_NIGHT = 14000;
    static constexpr int32_t TIME_SUNRISE = 23000;
    static constexpr int32_t TIME_FULL = 24000;

    // Sets the skip-night countdown; non-positive values cancel it.
    void setSleepTicks(int32_t t) {
        sleep_ticks_ = t;
    }

    // Checks whether the pending sleep vote can skip the night.
    void checkSleep();

    // ----- Weather system -----
    // Uses MCPE 0.15.10 rain and thunder events with non-zero strengths.
    enum WeatherState : int {
        WEATHER_CLEAR = 0,
        WEATHER_RAIN = 1,
        WEATHER_RAINY_THUNDER = 2,
        WEATHER_THUNDER = 3,
    };

    int weatherState() const {
        return weather_state_;
    }

    bool isRaining() const {
        return weather_state_ == WEATHER_RAIN || weather_state_ == WEATHER_RAINY_THUNDER;
    }

    bool isThundering() const {
        return weather_state_ == WEATHER_RAINY_THUNDER || weather_state_ == WEATHER_THUNDER;
    }

    // Returns remaining weather ticks; -1 means unlimited duration.
    int32_t weatherTicksLeft() const {
        return weather_ticks_left_;
    }

    // Changes the weather and broadcasts its packet pair to online players.
    void setWeather(int state, int32_t durationTicks = 0);

    // Sends the current weather state to a joining player.
    void sendWeatherTo(class Player* p);

    // Updates weather duration and processes rain-related fire extinguishing.
    static constexpr int WEATHER_CHUNKS_PER_TICK = 8;
    static constexpr int MAX_EXTINGUISH_PER_TICK = 40;
    static constexpr uint64_t FIRE_EXTINGUISH_LOG_INTERVAL = 40;
    static constexpr uint64_t LIGHTNING_STRIKE_INTERVAL = 200;
    void doWeatherTick(uint64_t current_tick);

    // ----- Lightning system -----
    // Spawns lightning, applies damage, and optionally ignites the strike location.
    void spawnLightning(double x, double y, double z, bool withFire);

    // Returns the world spawn position.
    mcp_vector3_t spawn() const {
        return spawn_;
    }

    void setSpawn(mcp_vector3_t v) {
        spawn_ = v;
    }

    // Saves all dirty chunks and world data.
    int save();

    // Saves a limited number of chunks within a time budget.
    int saveBudget(size_t max_chunks, uint64_t max_ms, bool write_level_data);

    // Returns the number of chunks with unsaved changes.
    size_t unsavedChunkCount() const;

    // Advances world time and weather; entity cleanup is handled globally.
    void tick(uint64_t current_tick);

    /// @brief Starts a new tick sample and updates the moving average.
    /// @note Main thread only.
    void beginTickSample(double ms) {
        avgTickMs_ = avgTickMs_ * 0.95 + lastTickMs_ * 0.05;
        lastTickMs_ = ms;
    }

    /// @brief Adds time spent in a later tick phase.
    /// @note Main thread only.
    void addTickSample(double ms) {
        lastTickMs_ += ms;
    }

    /// @brief Returns the duration of the last completed world tick.
    double lastTickMs() const {
        return lastTickMs_;
    }

    /// @brief Returns the moving average of world tick duration.
    double avgTickMs() const {
        return avgTickMs_;
    }

    // Updates dropped-item physics, movement packets, and player attraction.
    void tickDroppedItems(uint64_t current_tick);

    // Updates arrow physics, player hits, block collisions, and despawning.
    void tickArrows(uint64_t current_tick);
    void addArrow(ArrowEntity a);
    std::vector<ArrowEntity*> arrows();

    // Atomically claims an arrow for pickup.
    bool tryClaimArrow(int64_t eid);

    // Broadcasts a particle to players who can see its chunk.
    void addParticle(const particle::Particle& p);

    // ----- Falling-block physics -----
    // Converts a supported falling block into an entity and checks the block above.
    void spawnFallingBlock(int32_t bx, int32_t by, int32_t bz);

    // Updates falling-block physics, landing, damage, sounds, and cleanup.
    void tickFallingBlocks(uint64_t current_tick);

    // Converts unsupported sand, gravel, or anvils into falling entities.
    void checkFallingSupport(int32_t x, int32_t y, int32_t z);

    // Checks whether a block follows gravity in MCPE 0.15.10.
    static bool isFallableBlockId(uint8_t id);

    // Drops an item at a world position without player context.
    void dropItemAt(double x, double y, double z, uint16_t itemId, uint8_t count, uint16_t damage);

    // Updates block entities such as furnaces once per server tick.
    size_t tickTiles(uint64_t current_tick);

    // Removes expired or claimed dropped items during global garbage collection.
    size_t collectDroppedItems(uint64_t current_tick);

    // Returns the configured world generator.
    std::shared_ptr<generator::Generator> generator() const {
        return generator_;
    }

    // Returns the active world provider.
    std::shared_ptr<format::WorldProvider> provider() const {
        return provider_;
    }

    // Registers or unregisters chunk loaders.
    void registerLoader(uint64_t loader_id, class ChunkLoader* loader);
    void unregisterLoader(uint64_t loader_id);

    // ----- Dropped-item registry -----
    // Returns a non-owning pointer to a dropped item, or nullptr.
    DroppedItem* droppedItem(int64_t eid);

    // Returns a snapshot of dropped items for pickup scans.
    std::vector<DroppedItem*> droppedItems();

    // Registers a newly dropped item.
    void addDroppedItem(const DroppedItem& di);

    // Marks an item for removal.
    void removeDroppedItem(int64_t eid);

    // Atomically claims an item to prevent duplicate pickups.
    bool tryClaimDroppedItem(int64_t eid);

    // ----- Tile-entity registry -----
    // Maintains persistent tile instances for chests, furnaces, signs, and other blocks.
    std::shared_ptr<cursestone::tile::Tile> getTile(int32_t x, int32_t y, int32_t z);
    void setTile(int32_t x, int32_t y, int32_t z, std::shared_ptr<cursestone::tile::Tile> t);
    void removeTile(int32_t x, int32_t y, int32_t z);

    // Returns all tile entities in a chunk for client synchronization.
    std::vector<std::shared_ptr<cursestone::tile::Tile>> getTilesInChunk(int32_t cx, int32_t cz);

    // ----- Chunk memory eviction -----
    // Stores a player's current chunk and configured view radius.
    struct ChunkViewer {
        int32_t cx, cz, view_radius;
    };

    // Unloads chunks outside all viewer radii, saving dirty chunks first.
    int unloadDistantChunks(const std::vector<ChunkViewer>& viewers, int32_t margin = 4);

  private:
    double lastTickMs_ = 0.0;
    double avgTickMs_ = 0.0;

    // Flushes a chunk's tiles and removes their in-memory registry entries.
    void evictTilesForChunk(int32_t cx, int32_t cz);

    // Loads tile entities from a chunk's stored NBT data.
    void ensureTilesLoaded(int32_t cx, int32_t cz);

    // Creates missing tile entities for blocks that require them.
    void hydrateTilesFromBlocks(int32_t cx, int32_t cz, mcp_chunk_t* chunk);

    // Writes dirty tile entities back into their owning chunks before saving.
    void syncTilesToChunks();

    WorldConfig cfg_;
    ChunkManager chunks_;
    std::shared_ptr<format::WorldProvider> provider_;
    std::shared_ptr<generator::Generator> generator_;
    PortalRegistry portals_;   // Persistent Nether portal registry.
    std::string portals_path_; // Path to the portal registry file.
    mcp_vector3_t spawn_;
    std::unordered_map<uint64_t, class ChunkLoader*> loaders_;

    // ----- Weather state -----
    int weather_state_ = WEATHER_CLEAR;
    int32_t weather_ticks_left_ = -1;       // -1 means unlimited duration.
    uint32_t rain_strength_ = 100000;       // Rain event strength.
    uint32_t thunder_strength_ = 35000;     // Thunder event strength.
    uint64_t rain_scan_offset_ = 0;         // Round-robin chunk scan cursor.
    uint64_t last_fire_extinguish_log_ = 0; // Fire log rate limit.
    uint64_t next_lightning_tick_ = 0;      // Next permitted lightning tick.

    // Skip-night countdown; 0 means inactive.
    int32_t sleep_ticks_ = 0;

    // Selects a random online player for a lightning strike during thunderstorms.
    void strikeRandomPlayerLightning();

    // Dropped items indexed by runtime entity ID.
    std::unordered_map<int64_t, std::unique_ptr<DroppedItem>> dropped_items_;
    mutable std::mutex dropped_items_mtx_;
    std::unordered_map<int64_t, std::unique_ptr<ArrowEntity>> arrows_;
    mutable std::mutex arrows_mtx_;

    // Falling blocks indexed by runtime entity ID.
    std::unordered_map<int64_t, std::unique_ptr<FallingBlockEntity>> falling_blocks_;
    mutable std::mutex falling_blocks_mtx_;

    // Tile entities indexed by packed block position.
    std::unordered_map<uint64_t, std::shared_ptr<cursestone::tile::Tile>> tiles_;
    mutable std::mutex tiles_mtx_;

    // Tracks chunks whose tile NBT has been loaded this session.
    std::unordered_set<uint64_t> tiles_hydrated_chunks_;

    // Tracks chunks whose stored tile NBT must be rebuilt after removals.
    std::unordered_set<uint64_t> tiles_dirty_chunks_;

    // Manages sky and block light propagation.
    LightEngine light_;

    static uint64_t packChunkPos(int32_t cx, int32_t cz) {
        uint64_t ux = static_cast<uint64_t>(static_cast<uint32_t>(cx));
        uint64_t uz = static_cast<uint64_t>(static_cast<uint32_t>(cz));
        return (ux << 32) | uz;
    }

    // Packs 28-bit X/Z coordinates and an 8-bit Y coordinate into one key.
    static uint64_t packTilePos(int32_t x, int32_t y, int32_t z) {
        uint64_t ux = static_cast<uint64_t>(static_cast<uint32_t>(x)) & 0xFFFFFFFULL;
        uint64_t uz = static_cast<uint64_t>(static_cast<uint32_t>(z)) & 0xFFFFFFFULL;
        uint64_t uy = static_cast<uint64_t>(static_cast<uint8_t>(y));
        return (ux << 36) | (uz << 8) | uy;
    }
};
} // namespace