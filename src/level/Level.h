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
#include <unordered_set>
#include <cstdint>
#include <functional>
#include <mutex>
#include "math/Vector3.h"
#include "level/ChunkManager.h"
#include "level/format/LevelProvider.h"
namespace atomikpe {
class Player; class Entity;
namespace tile { class Tile; }
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
    // Returns the block data (meta, 0..15) at world coords. Returns 0 if
    // the chunk is not loaded. Mirrors PMMP `Level::getBlockDataAt()`.
    // BlockFire propagation needs this to read the fire's "age" meta.
    uint8_t getBlockData(int32_t x, int32_t y, int32_t z);
    void setBlock(int32_t x, int32_t y, int32_t z, uint8_t id, uint8_t data);
    // Cheap probe — does NOT trigger chunk generation. BlockFire propagation
    // uses this to bail out of fire-spread into unloaded chunks instead of
    // silently treating them as AIR (which caused the runaway propagation
    // that froze the tick loop when using flint and steel).
    bool isChunkLoaded(int32_t x, int32_t z) {
        return chunks_.getChunkIfLoaded(x >> 4, z >> 4) != nullptr;
    }
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
    // Atomically try to claim a dropped item for pickup.
    // Returns true if this caller successfully claimed it (was not
    // already taken). Returns false if another player already took it.
    // This prevents the race condition where two players pass the
    // `di->taken` check in the same tick and both pick up the item.
    bool tryClaimDroppedItem(int64_t eid);

    // ----- Tile-entity registry -----
    // Tracks block-entities (chests, furnaces, signs, ...) keyed by
    // their block position, so the SAME instance is returned across
    // repeated opens instead of a fresh (empty) one being constructed
    // every time — this is what lets ChestInventory contents survive
    // a player closing and reopening the chest. Mirrors PMMP's
    // Level::tiles map (keyed by packed Vector3 hash there too).
    //
    // Persisted to disk: getTile() lazily hydrates a chunk's tiles from
    // its mcp_chunk_t::tile_entities blobs the first time that chunk is
    // touched (see ensureTilesLoaded()), and save() calls
    // syncTilesToChunks() to write every dirty tile's current NBT back
    // into its chunk's tile_entities array before ChunkManager::flushDirty()
    // hands the chunk to the LevelProvider. Chest contents now survive a
    // server restart, not just a close/reopen in the same session.
    std::shared_ptr<atomikpe::tile::Tile> getTile(int32_t x, int32_t y, int32_t z);
    void setTile(int32_t x, int32_t y, int32_t z, std::shared_ptr<atomikpe::tile::Tile> t);
    void removeTile(int32_t x, int32_t y, int32_t z);

    // ----- Chunk memory eviction -----
    // Snapshot of one connected player's position for the unload sweep
    // below: which chunk they're standing in, and how far out they can
    // actually see. `view_radius` is a RADIUS (Chebyshev distance), not
    // Player::viewDistance()'s raw config value — see the note on
    // buildChunkSpiral() in Player.cpp: despite the name, that config
    // value is closer to a diameter (buildChunkSpiral(vd) only ever
    // reaches |dx|,|dz| up to vd/2). Server::tick() is responsible for
    // passing viewDistance()/2, not the raw config number.
    struct ChunkViewer { int32_t cx, cz, view_radius; };

    // Frees the in-memory copy of any loaded chunk that sits outside
    // EVERY given viewer's radius (+ `margin` chunks of hysteresis, so
    // a player oscillating right at the boundary doesn't cause the same
    // chunk to unload and reload every few ticks). A dirty chunk is
    // saved before being freed (see ChunkManager::unloadChunk) and any
    // tile-entities it holds are flushed into it first (see
    // evictTilesForChunk below) — nothing here silently drops player
    // data just because it fell out of view.
    //
    // Call this periodically (not every tick — see Server::tick(),
    // which gates it to once every few seconds) with the current
    // position of every player connected to THIS level. An empty
    // `viewers` list (nobody connected) makes every loaded chunk
    // eligible, which is correct: a level nobody is looking at should
    // shed all of its chunks.
    //
    // Returns how many chunks were actually unloaded, for logging.
    int unloadDistantChunks(const std::vector<ChunkViewer>& viewers, int32_t margin = 4);

private:
    // Flushes any dirty tile-entity inside chunk (cx,cz) into that
    // chunk's mcp_chunk_t::tile_entities array (same write path as
    // syncTilesToChunks(), just scoped to one chunk) and then drops
    // every tile in that chunk from the in-memory tiles_ registry —
    // NOT just the dirty ones, and regardless of whether the flush
    // above ran (a chunk with zero dirty tiles still gets its tiles
    // dropped here). Also clears the chunk's tiles_hydrated_chunks_
    // flag so a future reload re-hydrates from the NBT that was just
    // written instead of either re-scanning nothing (flag left set) or
    // leaving getTile() pointing at a Tile object whose owning chunk
    // no longer exists in memory.
    //
    // Called by unloadDistantChunks() right before
    // chunks_.unloadChunk(cx, cz) for that same chunk — this is the
    // ONLY thing that keeps the tile registry from leaking in lockstep
    // with chunks the way ChunkManager used to leak chunks themselves.
    void evictTilesForChunk(int32_t cx, int32_t cz);

    // Hydrate tiles_ from chunk (cx,cz)'s mcp_chunk_t::tile_entities blobs,
    // if that chunk hasn't been hydrated yet this session. Called from
    // getTile() before the lookup, keyed by chunk (not by block position)
    // since the blobs live on the chunk, not per-position — cheap to call
    // on every getTile() because of the tiles_hydrated_chunks_ guard below.
    // No-op if the chunk isn't loaded (chunks_.getChunkIfLoaded returns
    // null) or has no tile_entities.
    void ensureTilesLoaded(int32_t cx, int32_t cz);
    // Write every dirty tile's current NBT into its owning chunk's
    // mcp_chunk_t::tile_entities array, replacing whatever was there
    // before. Called from save(), BEFORE chunks_.flushDirty() — that's
    // the only ordering that works, since flushDirty() is what actually
    // serializes each dirty chunk (tile_entities included) to the .mca
    // region file. A tile that was never marked dirty (Tile::isDirty())
    // is skipped, same as ChunkManager already skips chunks that aren't
    // dirty — but note a tile only marks its OWNING chunk dirty here
    // (via mcp_chunk_add_tile_entity), it doesn't touch block data, so a
    // chunk with only a dirty chest and no other change still gets saved.
    void syncTilesToChunks();

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

    // All tile-entities in this level, keyed by a packed (x,y,z) block
    // position. See getTile/setTile/removeTile above.
    std::unordered_map<uint64_t, std::shared_ptr<atomikpe::tile::Tile>> tiles_;
    mutable std::mutex tiles_mtx_;
    // Chunks whose tile_entities blobs have already been decoded into
    // tiles_ this session, keyed the same way ChunkManager keys chunks
    // (packed cx/cz). Prevents ensureTilesLoaded() from re-decoding the
    // same blobs (and re-constructing duplicate Tile objects) on every
    // single getTile() call for a chunk that's already hydrated.
    std::unordered_set<uint64_t> tiles_hydrated_chunks_;
    static uint64_t packChunkPos(int32_t cx, int32_t cz) {
        uint64_t ux = static_cast<uint64_t>(static_cast<uint32_t>(cx));
        uint64_t uz = static_cast<uint64_t>(static_cast<uint32_t>(cz));
        return (ux << 32) | uz;
    }
    // x/z each get 28 bits (>±134M blocks — far beyond any MCPE 0.15.x
    // world border), y gets 8 bits (0..255, the full legacy height
    // range). Negative x/z lose bits beyond that range rather than
    // colliding with a different position, which is an acceptable
    // trade-off for a legacy-protocol server with a small world border.
    static uint64_t packTilePos(int32_t x, int32_t y, int32_t z) {
        uint64_t ux = static_cast<uint64_t>(static_cast<uint32_t>(x)) & 0xFFFFFFFULL;
        uint64_t uz = static_cast<uint64_t>(static_cast<uint32_t>(z)) & 0xFFFFFFFULL;
        uint64_t uy = static_cast<uint64_t>(static_cast<uint8_t>(y));
        return (ux << 36) | (uz << 8) | uy;
    }
};
} // namespace
