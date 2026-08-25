// Level — a single world (overworld, nether, end). Holds the
// ChunkManager, generator, players, entities, tile-entities and
// per-world settings (time, weather, spawn position).
//
// IMPORTANT: on construction, the Level instantiates a LevelProvider
// (via LevelProviderManager) for the configured format and connects
// it to the ChunkManager so that flushDirty() actually writes to disk.
// Without this, the world is "in-memory only" and changes are lost
// when the server shuts down.
#pragma once
#include <string>
#include <memory>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <functional>
#include <mutex>
#include "math/Vector3.h"
#include "level/ChunkManager.h"
#include "level/format/LevelProvider.h"
namespace atomikpe {
class Player; class Entity; class Tile;
namespace level::generator { class Generator; }
}
namespace atomikpe::level {

// DroppedItem — a lightweight record of an item entity lying on the ground.
// Mirrors Nukkit's EntityItem (sans the full entity-tick machinery): we
// track position + the item + a pickup-delay so the player who just threw
// it doesn't instantly re-pick it up.
struct DroppedItem {
    int64_t  eid = 0;            // runtime entity id (matches AddItemEntityPacket)
    uint16_t itemId = 0;
    uint8_t  count  = 1;
    uint16_t damage = 0;
    double   x = 0.0, y = 0.0, z = 0.0;
    uint64_t spawn_tick = 0;     // when it was dropped (server tick)
    uint32_t pickup_delay_ticks = 10;  // ~0.5s at 20 TPS — Nukkit default
    bool     taken = false;      // marked for removal after pickup
};

struct LevelConfig {
    std::string name;
    std::string format   = "anvil";   // anvil (Nukkit Anvil, default) | leveldb
    std::string generator = "normal"; // normal | flat | nether | void
    int64_t seed = 0;
    int32_t time = 0;
    int32_t spawn_x = 0, spawn_y = 64, spawn_z = 0;
    int32_t view_distance = 8;
    std::string dataPath;   // absolute path to the server's data dir
};
class Level {
public:
    explicit Level(LevelConfig cfg);
    ~Level();
    const std::string& name() const { return cfg_.name; }
    int64_t seed() const { return cfg_.seed; }
    const LevelConfig& config() const { return cfg_; }
    ChunkManager& chunks() { return chunks_; }
    // Returns the block id at world coords. Loads chunk if needed.
    uint8_t getBlockId(int32_t x, int32_t y, int32_t z);
    void setBlock(int32_t x, int32_t y, int32_t z, uint8_t id, uint8_t data);
    // World time (0..24000). Incremented each tick (20/s).
    int32_t time() const { return cfg_.time; }
    void setTime(int32_t t) { cfg_.time = t; }
    void advanceTime(int dt_ticks) { cfg_.time = (cfg_.time + dt_ticks) % 24000; }
    // Spawn point.
    mcp_vector3_t spawn() const { return spawn_; }
    void setSpawn(mcp_vector3_t v) { spawn_ = v; }
    // Save all dirty chunks (routes to the LevelProvider).
    int save();
    // Tick this level — entities, tile entities, scheduled blocks.
    void tick(uint64_t current_tick);
    // Generator accessor for AsyncPool workers.
    std::shared_ptr<generator::Generator> generator() const { return generator_; }
    // The active LevelProvider (Anvil by default).
    std::shared_ptr<format::LevelProvider> provider() const { return provider_; }
    // ChunkLoaders (players) registered to receive chunk updates.
    void registerLoader(uint64_t loader_id, class ChunkLoader* loader);
    void unregisterLoader(uint64_t loader_id);

    // ----- Dropped-item entity registry -----
    // Tracks items lying on the ground so players can pick them up.
    // Mirrors Nukkit's Level.getNearbyEntities() filtered by EntityItem.
    // Returns a borrowed pointer (non-owning) or nullptr if not found.
    DroppedItem* droppedItem(int64_t eid);
    // Snapshot of all dropped items (for pickup scans).
    std::vector<DroppedItem*> droppedItems();
    // Register a freshly-dropped item. The Level takes ownership of the
    // record's lifetime (it removes itself when `taken` is true).
    void addDroppedItem(const DroppedItem& di);
    // Mark an item as taken (will be erased on next tick).
    void removeDroppedItem(int64_t eid);

private:
    LevelConfig cfg_;
    ChunkManager chunks_;
    std::shared_ptr<format::LevelProvider> provider_;
    std::shared_ptr<generator::Generator> generator_;
    mcp_vector3_t spawn_;
    std::unordered_map<uint64_t, class ChunkLoader*> loaders_;

    // All dropped items in this level, keyed by runtime entity id.
    // Tick sweep removes items with taken==true.
    std::unordered_map<int64_t, std::unique_ptr<DroppedItem>> dropped_items_;
    mutable std::mutex dropped_items_mtx_;
};
} // namespace
