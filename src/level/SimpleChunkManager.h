// SPDX-License-Identifier: LGPL-3.0-or-later
// SimpleChunkManager.h — ported from cn.nukkit.level.SimpleChunkManager
//
// A simple in-memory chunk manager used by generators and tests.
// Stores chunks in an unordered_map keyed by (chunkX, chunkZ) hash.
// Unlike the full ChunkManager (which connects to a LevelProvider),
// SimpleChunkManager has no disk I/O — it's pure in-memory.
#pragma once
#include "ChunkManager.h"
#include "format/Chunk.h"
#include <unordered_map>
#include <cstdint>
#include <functional>

namespace cursestone::level {

class SimpleChunkManager : public ChunkManager {
public:
    explicit SimpleChunkManager(int64_t seed) : seed_(seed) {}

    int64_t getSeed() const override { return seed_; }

    mcp_chunk_t* getChunkIfLoaded(int32_t x, int32_t z) const override {
        auto it = chunks_.find(hash(x, z));
        return it == chunks_.end() ? nullptr : it->second;
    }

    mcp_chunk_t* getChunk(int32_t x, int32_t z) override {
        return getChunkIfLoaded(x, z);
    }

    void setChunk(int32_t x, int32_t z, mcp_chunk_t* chunk) override {
        auto key = hash(x, z);
        auto it = chunks_.find(key);
        if (it != chunks_.end() && it->second) {
            mcp_chunk_destroy(it->second);
        }
        chunks_[key] = chunk;
    }

    void markDirty(int32_t /*x*/, int32_t /*z*/) override {}
    int flushDirty() override { return 0; }

    void setProvider(std::shared_ptr<format::LevelProvider> p) override {
        provider_ = std::move(p);
    }
    std::shared_ptr<format::LevelProvider> provider() const override {
        return provider_;
    }
    size_t count() const override { return chunks_.size(); }

    void cleanChunks() { chunks_.clear(); }

private:
    static int64_t hash(int32_t x, int32_t z) {
        return ((int64_t)x << 32) | ((uint32_t)z);
    }
    int64_t seed_;
    std::unordered_map<int64_t, mcp_chunk_t*> chunks_;
    std::shared_ptr<format::LevelProvider> provider_;
};

} // namespace cursestone::level
