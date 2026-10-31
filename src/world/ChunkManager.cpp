/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

 * CurseStone | Open Source Server Software
 *
 * Copyright (C) 2026 CurseStoneTeam
 *
 * Developed by CurseStoneTeam.
 * Built for an open and extensible Minecraft server ecosystem.
 *
 * Licensed under the GNU Lesser General Public License v3.0 or later.
 * You may study, modify, and redistribute this software under its license.
 *
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 * Repository: https://github.com/CurseStoneTeam/CurseStone
 *
 */
// SPDX-License-Identifier: LGPL-3.0-or-later
// ChunkManager.cpp — caches chunks in memory and routes load/save
// requests to the active WorldProvider. Mirrors the reference server's
// ChunkManager role.
//
// CRITICAL: When a chunk is not on disk, ChunkManager asks the
// World's generator (Normal/Flat/Nether) to generate it. This
// replaces the old hardcoded generateFlatChunk() that always
// produced flat terrain regardless of the configured generator.
#include "world/format/WorldProvider.h"
#include "world/format/FullChunk.h"
#include "world/format/Chunk.h"
#include "world/format/ChunkAdapter.h"
#include "world/format/ChunkSection.h"
#include "world/generator/Generator.h"
#include "world/generator/GenerationTask.h"
#include "scheduler/AsyncPool.h"
#include "ChunkManager.h"
#include <chrono>
#include <string.h>

namespace cursestone::world {

ChunkManager::ChunkManager() {
}
ChunkManager::~ChunkManager() {
    // Generation workers may still be running while the destructor runs
    // (process shutdown drains the pool first, but be robust); hold the
    // same lock the workers take when they install chunks.
    std::lock_guard<std::mutex> lk(mtx_);
    for (auto& kv : chunks_)
        if (kv.second)
            mcp_chunk_destroy(kv.second);
}

mcp_chunk_t* ChunkManager::getChunkIfLoaded(int32_t x, int32_t z) const {
    std::lock_guard<std::mutex> lk(mtx_);
    auto it = chunks_.find({x, z});
    return it == chunks_.end() ? nullptr : it->second;
}

mcp_chunk_t* ChunkManager::getChunk(int32_t x, int32_t z) {
    if (mcp_chunk_t* c = getChunkIfLoaded(x, z))
        return c;

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

    // Not on disk — use the World's generator to create it.
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
            // A throw here must not strand the half-built chunk (mirrors
            // the async task's own cleanup).
            try {
                generator_->generate(ctx);
                generator_->populate(ctx);
            } catch (...) {
                mcp_chunk_destroy(c);
                throw;
            }

            // Mark as populated + dirty (so it gets saved to disk).
            c->terrain_populated = true;
            c->dirty = true;

            // REAL BUG (black world/grass): sky_light used to be
            // memset to 0xFF before terrain existed and light was never
            // really computed. Fix: heightmap first, then the real light
            // pass (which sets light_populated itself).
            mcp_chunk_recalculate_height_map(c);
            mcp_chunk_propagate_sky_light(c);

            setChunk(x, z, c);
            return c;
        }
    }

    // Fallback: generate a basic flat chunk if no generator is set.
    // This should never happen in normal operation — the World always
    // sets a generator. But it prevents a crash if something goes wrong.
    mcp_chunk_t* c = mcp_chunk_new(x, z);
    if (c) {
        c->terrain_populated = true;
        c->dirty = true;
        for (int bx = 0; bx < 16; ++bx) {
            for (int bz = 0; bz < 16; ++bz) {
                mcp_chunk_set_block_id(c, bx, 0, bz, 7); // BEDROCK
                mcp_chunk_set_block_id(c, bx, 1, bz, 3); // DIRT
                mcp_chunk_set_block_id(c, bx, 2, bz, 3); // DIRT
                mcp_chunk_set_block_id(c, bx, 3, bz, 2); // GRASS
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

mcp_chunk_t* ChunkManager::getChunkOrRequestAsync(int32_t x, int32_t z,
                                                  scheduler::AsyncPool* pool) {
    // Already in memory (loaded from disk earlier, or a previous async
    // generation already completed) — return immediately, no blocking.
    if (mcp_chunk_t* c = getChunkIfLoaded(x, z))
        return c;

    // No pool (or no generator) available — fall back to the old
    // synchronous path rather than returning nullptr forever. This
    // keeps single-threaded builds / early boot / tests working.
    if (!pool || !generator_)
        return getChunk(x, z);

    ChunkKey key{x, z};
    {
        std::lock_guard<std::mutex> lk(pending_mtx_);
        if (pending_.count(key))
            return nullptr; // already in flight
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
            if (handed_to_pool)
                return; // async task/callback owns the erase now
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
                    return raw; // guard erases pending_ on the way out
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
        generator_, x, z, seed, [this, key](mcp_chunk_t* c) {
            // GenerationTask::run() already filled light/biome defaults
            // and ran generate()+populate() before calling us — just
            // hand the finished chunk to the map and clear the in-flight
            // marker.
            //
            // RACE FIX ("a veces los chunks no se renderizan" + ediciones
            // disappearing edits): while this task ran on a worker, the MAIN
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
                std::scoped_lock lk(mtx_, pending_mtx_);
                auto it = chunks_.find(key);
                if (it == chunks_.end() || !it->second) {
                    // Replay neighbor writes queued while this chunk was
                    // generating, then install it.
                    if (spillQueue_)
                        spillQueue_->drainSpills(key.x, key.z, c);
                    chunks_[key] = c;
                    count_.store(chunks_.size(), std::memory_order_relaxed);
                    installed = true;
                }
            }
            if (c && !installed)
                mcp_chunk_destroy(c);
            {
                std::lock_guard<std::mutex> lk(pending_mtx_);
                pending_.erase(key);
            }
            // Outside every lock (same contract as setChunk): the World
            // only queues work — here the dynamic-liquid wake scan, so a
            // chunk that was generated on a worker also starts its liquid
            // ticking queue the moment it goes live.
            if (installed && on_chunk_installed_)
                on_chunk_installed_(key.x, key.z);
        });

    if (pool->submit([task] {
            task->run();
        })) {
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
    {
        std::scoped_lock lk(mtx_, pending_mtx_);
        auto key = ChunkKey{x, z};
        auto it = chunks_.find(key);
        if (it != chunks_.end() && it->second && it->second != chunk)
            mcp_chunk_destroy(it->second);
        chunks_[key] = chunk;
        count_.store(chunks_.size(), std::memory_order_relaxed);
        // Replay generation-time writes that were queued for this chunk
        // while it was absent (neighbors populated first). Done under the
        // same locks the generator's commitNeighborWrite() uses, so a
        // write can never land between "queued" and "live".
        if (spillQueue_)
            spillQueue_->drainSpills(x, z, chunk);
    }
    // Outside the lock: the callback (World) only enqueues the chunk for a
    // later light sweep. Calling it with mtx_ held would deadlock on the
    // very mutex ChunkManager needs.
    if (on_chunk_added_)
        on_chunk_added_(x, z);
    if (on_chunk_installed_)
        on_chunk_installed_(x, z);
}

void ChunkManager::commitNeighborWrite(int cx, int cz, uint32_t index, uint8_t id, uint8_t data) {
    mcp_chunk_t* live = nullptr;
    {
        std::scoped_lock lk(mtx_, pending_mtx_);
        auto it = chunks_.find(ChunkKey{cx, cz});
        if (it != chunks_.end() && it->second) {
            live = it->second;
        } else if (spillQueue_) {
            // Chunk not live yet: park the write for its populate/install.
            // Holding both locks here makes the queue-vs-live decision
            // atomic against the installer (drainSpills in setChunk and in
            // the async callback), so the write cannot be swallowed.
            spillQueue_->queueSpill(cx, cz, index, id, data);
        }
    }
    if (live) {
        uint32_t y = index % 128;
        uint32_t rest = index / 128;
        uint32_t z = rest % 16;
        uint32_t x = rest / 16;
        mcp_chunk_set_block_full(live, (int)x, (int)y, (int)z, id, data);
        // The new blocks change the sky light the same way a player edit
        // would — requeue the light sweep for the chunk.
        if (on_chunk_added_)
            on_chunk_added_(cx, cz);
    }
}

void ChunkManager::markDirty(int32_t x, int32_t z) {
    mcp_chunk_t* c = getChunkIfLoaded(x, z);
    if (c)
        c->dirty = true;
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
        if (!kv.second || !kv.second->dirty)
            continue;
        auto adapter = format::ChunkAdapter::borrow(kv.second);
        if (adapter) {
            provider_->saveChunk(adapter);
            kv.second->dirty = false;
            ++n;
        }
    }
    return n;
}

// Flush acotado: el que usa el autosave. La diferencia con flushDirty()
// is the BUDGET: write a handful of chunks per call and stop when the
// time budget runs out, so saving the world no longer spikes hundreds
// of ms into one tick (measured: ~304 ms for 400 real chunks) and is
// spread across ticks instead (<6 ms each).
//
// mtx_ IS held during the I/O, like flushDirty(): console commands run
// on their own thread, so /save can enter mid-drain and the anvil
// provider is not thread-safe; the lock serializes both writers. The
// difference is that the lock is now released within a few ms, which is
// the most an AsyncPool worker waiting on setChunk() can lose.
int ChunkManager::flushDirtyBudget(size_t max_chunks, uint64_t max_ms) {
    if (max_chunks == 0)
        return 0;

    std::lock_guard<std::mutex> lk(mtx_);
    int n = 0;
    auto t0 = std::chrono::steady_clock::now();
    for (auto& kv : chunks_) {
        if (!kv.second || !kv.second->dirty)
            continue;
        if (provider_) {
            auto adapter = format::ChunkAdapter::borrow(kv.second);
            if (!adapter)
                continue;
            provider_->saveChunk(adapter);
        }
        // Note: the flag is cleared either way, like flushDirty() —
        // saveChunk() failures are already logged by the provider. Chunks
        // that cannot be wrapped in a ChunkAdapter are skipped uncounted
        // corta el drenaje tras 40 ticks sin progreso, para no
        // reintentarlo eternamente.
        kv.second->dirty = false;
        ++n;
        if ((size_t)n >= max_chunks)
            break;
        if (max_ms > 0) {
            uint64_t used = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - t0)
                                .count();
            if (used >= max_ms)
                break;
        }
    }
    return n;
}

size_t ChunkManager::dirtyCount() const {
    std::lock_guard<std::mutex> lk(mtx_);
    size_t n = 0;
    for (auto& kv : chunks_) {
        if (kv.second && kv.second->dirty)
            ++n;
    }
    return n;
}

size_t ChunkManager::count() const {
    return count_.load(std::memory_order_relaxed);
}

bool ChunkManager::unloadChunk(int32_t x, int32_t z, bool save) {
    mcp_chunk_t* c = nullptr;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        auto it = chunks_.find({x, z});
        if (it == chunks_.end())
            return false;
        c = it->second;
        chunks_.erase(it);
        count_.store(chunks_.size(), std::memory_order_relaxed);
    }
    if (!c)
        return false;
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

std::vector<std::pair<int32_t, int32_t>> ChunkManager::loadedChunkCoords() const {
    std::lock_guard<std::mutex> lk(mtx_);
    std::vector<std::pair<int32_t, int32_t>> out;
    out.reserve(chunks_.size());
    for (auto& kv : chunks_)
        out.emplace_back(kv.first.x, kv.first.z);
    return out;
}

} // namespace
