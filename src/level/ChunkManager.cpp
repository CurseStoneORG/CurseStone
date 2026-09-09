// SPDX-License-Identifier: LGPL-3.0-or-later
// ChunkManager.cpp — caches chunks in memory and routes load/save
// requests to the active LevelProvider. Mirrors pocketmine's
// ChunkManager role.
//
// CRITICAL: When a chunk is not on disk, ChunkManager asks the
// Level's generator (Normal/Flat/Nether) to generate it. This
// replaces the old hardcoded generateFlatChunk() that always
// produced flat terrain regardless of the configured generator.
#include "level/format/LevelProvider.h"
#include "level/format/FullChunk.h"
#include "level/format/Chunk.h"
#include "level/format/ChunkAdapter.h"
#include "level/format/ChunkSection.h"
#include "level/generator/Generator.h"
#include "level/generator/GenerationTask.h"
#include "scheduler/AsyncPool.h"
#include "ChunkManager.h"
#include <string.h>

namespace atomikpe::level {

ChunkManager::ChunkManager() {}
ChunkManager::~ChunkManager() {
    for (auto& kv : chunks_) if (kv.second) mcp_chunk_destroy(kv.second);
}

mcp_chunk_t* ChunkManager::getChunkIfLoaded(int32_t x, int32_t z) const {
    std::lock_guard<std::mutex> lk(mtx_);
    auto it = chunks_.find({x,z});
    return it == chunks_.end() ? nullptr : it->second;
}

mcp_chunk_t* ChunkManager::getChunk(int32_t x, int32_t z) {
    if (mcp_chunk_t* c = getChunkIfLoaded(x,z)) return c;

    // First, try loading from the provider (disk). If the chunk
    // exists on disk, we get back a fully populated mcp_chunk_t*.
    if (provider_) {
        auto loaded = provider_->loadChunk(x, z);
        if (loaded.has_value()) {
            std::shared_ptr<format::FullChunk>& fc = loaded.value();
            auto* adapter = dynamic_cast<format::ChunkAdapter*>(fc.get());
            if (adapter && adapter->chunk()) {
                mcp_chunk_t* raw = adapter->releaseChunk();
                if (raw) {
                    setChunk(x, z, raw);
                    return raw;
                }
            }
        }
    }

    // Not on disk — use the Level's generator to create it.
    // This replaces the old hardcoded generateFlatChunk(). The
    // generator (Normal/Flat/Nether) is set via setGenerator().
    if (generator_) {
        mcp_chunk_t* c = mcp_chunk_new(x, z);
        if (c) {
            // Fill sky_light with 0xFF for all allocated sections.
            for (int s = 0; s < 16; ++s) {
                mcp_chunk_section_t* sec = mcp_chunk_get_or_alloc_section(c, (uint8_t)s);
                if (sec) {
                    if (sec->sky_light) memset(sec->sky_light, 0xFF, 2048);
                    if (sec->block_light) memset(sec->block_light, 0x00, 2048);
                }
            }
            // Fill biomes with plains (1) by default.
            for (int bx = 0; bx < 16; ++bx) {
                for (int bz = 0; bz < 16; ++bz) {
                    mcp_chunk_set_biome(c, bx, bz, 1);
                }
            }

            // Call the generator to fill terrain + features.
            generator::GenerationContext ctx;
            // Get seed from provider or use 0.
            ctx.seed = provider_ ? provider_->getSeed() : 0;
            ctx.chunk_x = x;
            ctx.chunk_z = z;
            ctx.biomes_in = nullptr;
            ctx.chunk = c;
            generator_->generate(ctx);
            generator_->populate(ctx);

            // Mark as populated + dirty (so it gets saved to disk).
            c->terrain_populated = true;
            c->light_populated = true;
            c->dirty = true;

            // Recalculate height map after generation.
            mcp_chunk_recalculate_height_map(c);

            setChunk(x, z, c);
            return c;
        }
    }

    // Fallback: generate a basic flat chunk if no generator is set.
    // This should never happen in normal operation — the Level always
    // sets a generator. But it prevents a crash if something goes wrong.
    mcp_chunk_t* c = mcp_chunk_new(x, z);
    if (c) {
        c->terrain_populated = true;
        c->light_populated = true;
        c->dirty = true;
        for (int bx = 0; bx < 16; ++bx) {
            for (int bz = 0; bz < 16; ++bz) {
                mcp_chunk_set_block_id(c, bx, 0, bz, 7);  // BEDROCK
                mcp_chunk_set_block_id(c, bx, 1, bz, 3);  // DIRT
                mcp_chunk_set_block_id(c, bx, 2, bz, 3);  // DIRT
                mcp_chunk_set_block_id(c, bx, 3, bz, 2);  // GRASS
            }
        }
        for (int s = 0; s < 16; ++s) {
            mcp_chunk_section_t* sec = mcp_chunk_get_or_alloc_section(c, (uint8_t)s);
            if (sec) {
                if (sec->sky_light) memset(sec->sky_light, 0xFF, 2048);
                if (sec->block_light) memset(sec->block_light, 0x00, 2048);
            }
        }
        for (int bx = 0; bx < 16; ++bx) {
            for (int bz = 0; bz < 16; ++bz) {
                mcp_chunk_set_biome(c, bx, bz, 1);
            }
        }
        mcp_chunk_recalculate_height_map(c);
        setChunk(x, z, c);
    }
    return c;
}

mcp_chunk_t* ChunkManager::getChunkOrRequestAsync(int32_t x, int32_t z, scheduler::AsyncPool* pool) {
    // Already in memory (loaded from disk earlier, or a previous async
    // generation already completed) — return immediately, no blocking.
    if (mcp_chunk_t* c = getChunkIfLoaded(x, z)) return c;

    // No pool (or no generator) available — fall back to the old
    // synchronous path rather than returning nullptr forever. This
    // keeps single-threaded builds / early boot / tests working.
    if (!pool || !generator_) return getChunk(x, z);

    ChunkKey key{x, z};
    {
        std::lock_guard<std::mutex> lk(pending_mtx_);
        if (pending_.count(key)) return nullptr;   // already in flight
        pending_.insert(key);
    }

    // Try disk first — cheap enough to do on the calling (main) thread;
    // only actual terrain generation goes to a worker.
    if (provider_) {
        auto loaded = provider_->loadChunk(x, z);
        if (loaded.has_value()) {
            std::shared_ptr<format::FullChunk>& fc = loaded.value();
            auto* adapter = dynamic_cast<format::ChunkAdapter*>(fc.get());
            if (adapter && adapter->chunk()) {
                mcp_chunk_t* raw = adapter->releaseChunk();
                if (raw) {
                    setChunk(x, z, raw);
                    std::lock_guard<std::mutex> lk(pending_mtx_);
                    pending_.erase(key);
                    return raw;
                }
            }
        }
    }

    int64_t seed = provider_ ? provider_->getSeed() : 0;
    auto task = std::make_shared<generator::GenerationTask>(
        generator_, x, z, seed,
        [this, key](mcp_chunk_t* c) {
            // GenerationTask::run() already filled light/biome defaults
            // and ran generate()+populate() before calling us — just
            // hand the finished chunk to the map (thread-safe on its
            // own) and clear the in-flight marker.
            if (c) setChunk(c->x, c->z, c);
            std::lock_guard<std::mutex> lk(pending_mtx_);
            pending_.erase(key);
        });

    if (!pool->submit([task]{ task->run(); })) {
        // Pool refused (shutting down) — undo the pending mark so we
        // don't leak the slot; caller will just see nullptr this tick.
        std::lock_guard<std::mutex> lk(pending_mtx_);
        pending_.erase(key);
    }
    return nullptr;
}

void ChunkManager::setChunk(int32_t x, int32_t z, mcp_chunk_t* chunk) {
    std::lock_guard<std::mutex> lk(mtx_);
    auto key = ChunkKey{x, z};
    auto it = chunks_.find(key);
    if (it != chunks_.end() && it->second && it->second != chunk)
        mcp_chunk_destroy(it->second);
    chunks_[key] = chunk;
}

void ChunkManager::markDirty(int32_t x, int32_t z) {
    mcp_chunk_t* c = getChunkIfLoaded(x, z);
    if (c) c->dirty = true;
}

int ChunkManager::flushDirty() {
    std::lock_guard<std::mutex> lk(mtx_);
    int n = 0;
    if (!provider_) {
        for (auto& kv : chunks_) {
            if (kv.second && kv.second->dirty) {
                kv.second->dirty = false;
                ++n;
            }
        }
        return n;
    }
    for (auto& kv : chunks_) {
        if (!kv.second || !kv.second->dirty) continue;
        auto adapter = format::ChunkAdapter::borrow(kv.second);
        if (adapter) {
            provider_->saveChunk(adapter);
            kv.second->dirty = false;
            ++n;
        }
    }
    return n;
}

size_t ChunkManager::count() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return chunks_.size();
}

bool ChunkManager::unloadChunk(int32_t x, int32_t z, bool save) {
    mcp_chunk_t* c = nullptr;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        auto it = chunks_.find({x, z});
        if (it == chunks_.end()) return false;
        c = it->second;
        chunks_.erase(it);
    }
    if (!c) return false;
    // Save BEFORE destroying, and outside mtx_ — saveChunk() does disk
    // I/O, and holding the chunk-map lock across a write would block
    // every other getChunk()/setChunk() on this level for the duration.
    // The chunk is already removed from chunks_ at this point, so a
    // concurrent getChunk(x,z) would just regenerate/reload a fresh one
    // instead of touching this pointer — no use-after-free, just a
    // possible (harmless, rare) duplicate load if that race is hit.
    if (save && provider_ && c->dirty) {
        auto adapter = format::ChunkAdapter::borrow(c);
        if (adapter) {
            provider_->saveChunk(adapter);
            c->dirty = false;
        }
    }
    mcp_chunk_destroy(c);
    return true;
}

std::vector<std::pair<int32_t,int32_t>> ChunkManager::loadedChunkCoords() const {
    std::lock_guard<std::mutex> lk(mtx_);
    std::vector<std::pair<int32_t,int32_t>> out;
    out.reserve(chunks_.size());
    for (auto& kv : chunks_) out.emplace_back(kv.first.x, kv.first.z);
    return out;
}

} // namespace
