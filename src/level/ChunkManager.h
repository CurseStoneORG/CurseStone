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
