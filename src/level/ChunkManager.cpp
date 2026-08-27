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

} // namespace
