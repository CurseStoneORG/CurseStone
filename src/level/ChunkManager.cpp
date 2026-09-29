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

namespace cursestone::level {

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
            c->dirty = true;

            // BUG REAL (mundo/pasto negro): antes de este fix, acá se
            // hacía memset(sky_light, 0xFF) en las 16 secciones ANTES
            // de generar terreno, y luego nunca se recalculaba luz de
            // verdad — solo se marcaba light_populated=true. Fix:
            // primero heightmap (ya corregido para ignorar hojas/
            // flores), luego el pase real de propagación de luz, que
            // reemplaza por completo al memset viejo y deja
            // light_populated=true por su cuenta.
            mcp_chunk_recalculate_height_map(c);
            mcp_chunk_propagate_sky_light(c);

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
        c->dirty = true;
        for (int bx = 0; bx < 16; ++bx) {
            for (int bz = 0; bz < 16; ++bz) {
                mcp_chunk_set_block_id(c, bx, 0, bz, 7);  // BEDROCK
                mcp_chunk_set_block_id(c, bx, 1, bz, 3);  // DIRT
                mcp_chunk_set_block_id(c, bx, 2, bz, 3);  // DIRT
                mcp_chunk_set_block_id(c, bx, 3, bz, 2);  // GRASS
            }
        }
        for (int bx = 0; bx < 16; ++bx) {
            for (int bz = 0; bz < 16; ++bz) {
                mcp_chunk_set_biome(c, bx, bz, 1);
            }
        }
        // Same fix as the main generation path above: real heightmap
        // + real light propagation instead of a flat memset(0xFF).
        mcp_chunk_recalculate_height_map(c);
        mcp_chunk_propagate_sky_light(c);
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

    // FIX (root cause of "some chunks never render again after
    // rejoining"): `pending_` marks (x,z) as in-flight so we don't
    // submit duplicate generation work, and every past return path
    // out of this function was responsible for calling
    // pending_.erase(key) itself before returning. The disk-load path
    // below only did that erase on the SUCCESS branch (loadChunk() has
    // a value AND the dynamic_cast succeeds AND releaseChunk() returns
    // non-null). If loadChunk() returned a value but any of those
    // conditions failed — a chunk that exists on disk but whose
    // ChunkAdapter couldn't be reconstituted (partially-written NBT/
    // region record, adapter type mismatch, etc.) — execution fell
    // through to the generation branch below WITHOUT clearing
    // `pending_` first. From that point on, every future call to
    // getChunkOrRequestAsync() for that exact (x,z) saw
    // pending_.count(key) > 0 at the very top of this function and
    // returned nullptr immediately, forever — no retry, no fallback to
    // generation, nothing. The chunk was permanently stuck "in flight"
    // for the lifetime of the process.
    //
    // This is invisible on a brand-new world: nothing is on disk yet,
    // so loadChunk() never has a value and every chunk takes the
    // generation path, which always clears `pending_` correctly
    // (either in the async callback or the submit-failure branch).
    // It only starts happening once chunks that were saved to disk
    // (on unload/flushDirty) get re-requested on a later join — which
    // matches exactly the reported symptom: chunks render fine on
    // first entry, but some go missing/never load after leaving and
    // reconnecting.
    //
    // Fix: guarantee pending_.erase(key) runs on EVERY exit from this
    // point on, unless we've actually handed the chunk off to the
    // async pool (in which case the submitted task/callback owns
    // clearing it). A tiny RAII guard makes that automatic instead of
    // relying on every future return statement remembering to do it.
    bool handed_to_pool = false;
    struct PendingGuard {
        std::mutex& mtx;
        std::unordered_set<ChunkKey, ChunkKeyHash>& pending;
        ChunkKey key;
        bool& handed_to_pool;
        ~PendingGuard() {
            if (handed_to_pool) return;  // async task/callback owns the erase now
            std::lock_guard<std::mutex> lk(mtx);
            pending.erase(key);
        }
    } guard{pending_mtx_, pending_, key, handed_to_pool};

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
                    return raw;   // guard erases pending_ on the way out
                }
            }
            // loadChunk() had a value but we couldn't extract a usable
            // chunk from it (bad adapter / null release) — fall through
            // to generation below instead of leaving this chunk stuck.
            // The guard above ensures `pending_` gets cleared even
            // though we're about to re-insert it for the async submit.
        }
    }

    int64_t seed = provider_ ? provider_->getSeed() : 0;
    auto task = std::make_shared<generator::GenerationTask>(
        generator_, x, z, seed,
        [this, key](mcp_chunk_t* c) {
            // GenerationTask::run() already filled light/biome defaults
            // and ran generate()+populate() before calling us — just
            // hand the finished chunk to the map and clear the in-flight
            // marker.
            //
            // RACE FIX ("a veces los chunks no se renderizan" + ediciones
            // que desaparecían): while this task ran on a worker, the MAIN
            // thread could have loaded the same chunk from disk or had it
            // created/edited synchronously (getChunk() fallback, block
            // place/break, tile hydration). setChunk() blindly REPLACED
            // that live chunk with this freshly generated copy — losing
            // player edits and, worse, swapping chunk memory mid-tick
            // while other pointers to the old chunk were in use. Now: if
            // a chunk is already present in the map for this key, the
            // generated copy is discarded (destroyed — no leak) and the
            // live chunk stays. Generated content matches what would have
            // been produced anyway; the live copy may additionally carry
            // real edits, which are strictly newer information.
            bool installed = false;
            if (c) {
                std::lock_guard<std::mutex> lk(mtx_);
                auto it = chunks_.find(key);
                if (it == chunks_.end() || !it->second) {
                    chunks_[key] = c;
                    installed = true;
                }
            }
            if (c && !installed) mcp_chunk_destroy(c);
            std::lock_guard<std::mutex> lk(pending_mtx_);
            pending_.erase(key);
        });

    if (pool->submit([task]{ task->run(); })) {
        // Successfully handed off — the callback above now owns
        // clearing `pending_` once generation finishes. Tell the guard
        // not to erase it out from under the in-flight task.
        handed_to_pool = true;
    }
    // Pool refused (shutting down): handed_to_pool stays false, so the
    // guard erases pending_ on return — caller just sees nullptr this
    // tick and can retry later instead of the slot leaking forever.
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
