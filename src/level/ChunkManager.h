// ChunkManager — caches chunks in memory and routes load/save
// requests to the active LevelProvider. Mirrors pocketmine's
// ChunkManager role. The Level owns one of these. Generators run
// on the AsyncPool and post completed chunks back via setChunk().
#pragma once
#include "level/format/Chunk.h"
#include <unordered_map>
#include <memory>
#include <mutex>
namespace cenisyspe::level {
class LevelProvider;
class ChunkManager {
public:
    ChunkManager();
    ~ChunkManager();
    // Returns the cached chunk or NULL (does NOT trigger load).
    mcp_chunk_t* getChunkIfLoaded(int32_t x, int32_t z) const;
    // Returns the chunk, loading synchronously if missing. May
    // return NULL if the level provider can't load and no generator
    // is configured.
    mcp_chunk_t* getChunk(int32_t x, int32_t z);
    // Stores a chunk in the cache (takes ownership). If a chunk
    // already exists at this coord it is destroyed first.
    void setChunk(int32_t x, int32_t z, mcp_chunk_t* chunk);
    // Schedules a chunk for save at next flush.
    void markDirty(int32_t x, int32_t z);
    // Flushes dirty chunks to the provider. Called by Level::save().
    int flushDirty();
    // Sets the persistent provider (mcacpp/leveldb/mca).
    void setProvider(std::shared_ptr<LevelProvider> p) { provider_ = std::move(p); }
    std::shared_ptr<LevelProvider> provider() const { return provider_; }
    // Number of currently-cached chunks.
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
    std::shared_ptr<LevelProvider> provider_;
};
} // namespace
