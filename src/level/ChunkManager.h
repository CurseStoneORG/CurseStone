// ChunkManager — caches chunks in memory and routes load/save
// requests to the active LevelProvider. Mirrors pocketmine's
// ChunkManager role. The Level owns one of these. Generators run
// on the AsyncPool and post completed chunks back via setChunk().
#pragma once
#include "level/format/Chunk.h"
#include "level/format/LevelProvider.h"
#include <unordered_map>
#include <memory>
#include <mutex>
#include <vector>
#include <utility>
namespace atomikpe::level::generator { class Generator; }

namespace atomikpe::level {

class ChunkManager {
public:
    ChunkManager();
    ~ChunkManager();
    mcp_chunk_t* getChunkIfLoaded(int32_t x, int32_t z) const;
    mcp_chunk_t* getChunk(int32_t x, int32_t z);
    void setChunk(int32_t x, int32_t z, mcp_chunk_t* chunk);
    void markDirty(int32_t x, int32_t z);
    int flushDirty();
    // Frees a single chunk's memory. If `save` is true and a provider
    // is set, the chunk is saved first (only if it's actually dirty —
    // an unmodified chunk doesn't need a disk write just because it's
    // being evicted). Returns false if the chunk wasn't loaded.
    //
    // NOTE: this does NOT touch tile-entities living in that chunk —
    // Level::unloadDistantChunks() calls Level::evictTilesForChunk()
    // first to flush/drop them, because ChunkManager has no visibility
    // into Level::tiles_ (tile-entity registry lives one layer up).
    // Calling this directly without doing that first would still work
    // (the chunk's mcp_chunk_t::tile_entities blob only gets rewritten
    // by Level::syncTilesToChunks()/evictTilesForChunk() in the first
    // place), but any tile whose dirty NBT was never flushed into the
    // chunk stays unsaved, and Level::tiles_ would go on holding a Tile
    // object for a chunk that's no longer in memory.
    bool unloadChunk(int32_t x, int32_t z, bool save = true);
    // Coordinates of every chunk currently held in memory. The caller
    // (Level::unloadDistantChunks()) uses this to decide what's
    // eligible for eviction. Returns a snapshot copy rather than a
    // reference so the caller can iterate it without holding mtx_ for
    // the whole sweep (each unloadChunk() call above takes the lock
    // itself, per-chunk).
    std::vector<std::pair<int32_t,int32_t>> loadedChunkCoords() const;
    void setProvider(std::shared_ptr<format::LevelProvider> p) { provider_ = std::move(p); }
    std::shared_ptr<format::LevelProvider> provider() const { return provider_; }
    // Set the world generator used to create new chunks when they're
    // not on disk. Mirrors Nukkit where the generator is queried
    // for terrain + population.
    void setGenerator(std::shared_ptr<generator::Generator> g) { generator_ = std::move(g); }
    size_t count() const;
private:
    struct ChunkKey {
        int32_t x, z;
        bool operator==(const ChunkKey& o) const { return x==o.x && z==o.z; }
    };
    struct ChunkKeyHash {
        size_t operator()(const ChunkKey& k) const noexcept {
            return std::hash<int64_t>()((int64_t)k.x * 0x9e3779b97f4a7c15LL ^ (int64_t)k.z);
        }
    };
    mutable std::mutex mtx_;
    std::unordered_map<ChunkKey, mcp_chunk_t*, ChunkKeyHash> chunks_;
    std::shared_ptr<format::LevelProvider> provider_;
    std::shared_ptr<generator::Generator> generator_;
};
} // namespace
