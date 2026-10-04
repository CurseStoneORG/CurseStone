// ChunkManager — caches chunks in memory and routes load/save
// requests to the active LevelProvider. Mirrors pocketmine's
// ChunkManager role. The Level owns one of these. Generators run
// on the AsyncPool and post completed chunks back via setChunk().
#pragma once
#include "world/format/Chunk.h"
#include "world/format/LevelProvider.h"
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <mutex>
#include <vector>
#include <utility>
namespace cursestone::level::generator { class Generator; }
namespace cursestone::scheduler { class AsyncPool; }

namespace cursestone::level {

class ChunkManager {
public:
    ChunkManager();
    ~ChunkManager();
    mcp_chunk_t* getChunkIfLoaded(int32_t x, int32_t z) const;
    mcp_chunk_t* getChunk(int32_t x, int32_t z);

    // Non-blocking chunk fetch used by the per-player streaming path
    // (Player::onUpdate). Never generates on the calling thread:
    //   - if the chunk is already in memory, returns it immediately.
    //   - otherwise, if a generation request for (x,z) isn't already
    //     in flight, submits one to `pool` (runs generate()+populate()
    //     on a worker thread, per Generator's "stateless w.r.t. main
    //     thread" contract) and returns nullptr right away.
    //   - if a request IS already in flight, just returns nullptr
    //     again (no duplicate work submitted).
    // The caller should treat nullptr as "not ready yet, try again
    // next tick" rather than as a permanent miss.
    mcp_chunk_t* getChunkOrRequestAsync(int32_t x, int32_t z, scheduler::AsyncPool* pool);

    void setChunk(int32_t x, int32_t z, mcp_chunk_t* chunk);
    // Called every time a chunk enters the manager (generated, loaded
    // from disk, or replaced). Level installs a callback here so a chunk
    // that appears later than its neighbours still gets its lighting
    // swept together with them — without it a chunk keeps the lighting it
    // was generated with in isolation and shows a dark border where the
    // light should carry over. Must not block or touch ChunkManager:
    // setChunk() runs on the generation worker thread.
    void setOnChunkAdded(std::function<void(int32_t, int32_t)> cb) {
        on_chunk_added_ = std::move(cb);
    }
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
    std::function<void(int32_t, int32_t)> on_chunk_added_;
    std::unordered_map<ChunkKey, mcp_chunk_t*, ChunkKeyHash> chunks_;
    std::shared_ptr<format::LevelProvider> provider_;
    std::shared_ptr<generator::Generator> generator_;

    // Coordinates currently being generated on an AsyncPool worker,
    // so getChunkOrRequestAsync() doesn't submit the same chunk twice
    // while it's still in flight. Guarded by pending_mtx_ (separate
    // from mtx_ since the worker thread touches chunks_ via setChunk()
    // — which takes mtx_ itself — right before it clears this set).
    mutable std::mutex pending_mtx_;
    std::unordered_set<ChunkKey, ChunkKeyHash> pending_;
};
} // namespace
