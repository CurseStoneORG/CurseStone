/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

 * CurseStone | Open Source Server Software
 *
 * Copyright (C) 2026 CurseStoneORG
 *
 * Developed by CurseStoneORG.
 * Built for an open and extensible Minecraft server ecosystem.
 *
 * Licensed under the GNU Lesser General Public License v3.0 or later.
 * You may study, modify, and redistribute this software under its license.
 *
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 * Repository: https://github.com/CurseStoneORG/CurseStone
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
#include "world/performance/GenerationProfiler.h"
#include "tile/TileFactory.h"
#include "block/BlockIds.h"
#include "nbt/NBT.h"
#include "nbt/tag/CompoundTag.h"
#include "utils/CRaii.h"
#include "ChunkManager.h"
#include <algorithm>
#include <chrono>
#include <string>

namespace cursestone::world {

namespace {
using SteadyClock = std::chrono::steady_clock;
// Records wait-to-acquire time for one lock acquisition. t0 is captured
// just before the lock statement; call right after it. Pure observer.
inline void noteLockWait(ChunkManager::LockStats& s, const SteadyClock::time_point t0) {
    s.wait_ns.fetch_add(
        (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(SteadyClock::now() - t0)
            .count(),
        std::memory_order_relaxed);
    s.acquires.fetch_add(1, std::memory_order_relaxed);
}
} // namespace

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
    const auto t0 = SteadyClock::now();
    std::lock_guard<std::mutex> lk(mtx_);
    noteLockWait(chunk_lock_, t0);
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
        const auto t0 = SteadyClock::now();
        std::lock_guard<std::mutex> lk(pending_mtx_);
        noteLockWait(pending_lock_, t0);
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

    // One worker task does the whole "materialize this chunk" job:
    // disk first, generation on a miss. Both halves share the same
    // install callback below, so whichever source wins, the chunk goes
    // live through the identical path — spill replay, parked tile
    // blobs, light queue, liquid wake, registry hydration.
    //
    // RACE FIX ("a veces los chunks no se renderizan" + edits
    // disappearing): the install callback never REPLACES a chunk that
    // is already in the map. While this task runs on a worker, the
    // MAIN thread can load the same chunk from disk or have it
    // created/edited synchronously (getChunk() fallback, block
    // place/break, tile hydration) — a blind setChunk() would swap
    // chunk memory mid-tick and lose those edits. If a chunk is
    // already present for this key, the freshly materialized copy is
    // discarded and the live chunk stays: generated/disk content
    // matches what would have been produced anyway, and the live copy
    // may additionally carry real edits, which are strictly newer
    // information.
    auto install = [this, key](mcp_chunk_t* c) {
        bool installed = false;
        std::vector<std::vector<uint8_t>> drainedTiles;
        std::vector<char> preBlobMatches;
        if (c) {
            const auto t0 = SteadyClock::now();
            std::scoped_lock lk(mtx_, pending_mtx_);
            noteLockWait(chunk_lock_, t0); // pair acquired as one event
            auto it = chunks_.find(key);
            if (it == chunks_.end() || !it->second) {
                // Snapshot which tile blobs the chunk carries with a
                // standing block BEFORE the spill replay: a block a
                // replay kills was alive until now (drop its blob), one
                // already dead on disk is legacy damage (heal).
                preBlobMatches = snapshotTileBlobMatches(c);
                // Replay neighbor writes queued while this chunk was
                // materializing, then install it.
                if (spillQueue_)
                    spillQueue_->drainSpills(key.x, key.z, c);
                // Structure tile blobs parked for this chunk go into
                // its tile_entities before it is ever streamed — each
                // one only while its block survived the spill replay
                // above (the pair check runs after the blocks landed).
                takePendingTileBlobs(key, drainedTiles);
                keepBlockMatchedTileBlobs(c, drainedTiles);
                reconcileStoredTileBlobs(c, preBlobMatches);
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
        // only queues work — here the dynamic-liquid wake scan AND the
        // light sweep, so a chunk that was materialized on a worker
        // also starts its liquid ticking queue and gets its border pass
        // queued the moment it goes live.
        //
        // REAL BUG (the flight 'black stains, chunk by chunk'): this
        // callback used to fire ONLY the liquid hook — worker-produced
        // chunks never entered the light queue at all, so they were
        // serialized and sent with unrepaired cross-border seams; the
        // client kept those bytes and the seam stayed until the chunk
        // re-streamed. The worker DID light the interior; the border
        // pass is what makes it continuous with the neighbours.
        if (installed) {
            if (on_chunk_added_)
                on_chunk_added_(key.x, key.z, /*full=*/false);
            if (on_chunk_installed_)
                on_chunk_installed_(key.x, key.z);
            // Registry tiles for the drained blobs — outside every
            // lock, same contract as the hooks above. applyTileBlob
            // never clobbers a live tile, so a chunk that already
            // hydrated the blob loses nothing.
            if (on_tile_blob_) {
                for (const auto& blob : drainedTiles)
                    on_tile_blob_(blob.data(), blob.size());
            }
        }
    };

    int64_t seed = provider_ ? provider_->getSeed() : 0;
    auto task = std::make_shared<std::function<void()>>([this, x, z, seed, install]() {
        // 1) Disk first — region open + read + inflate + NBT decode,
        //    all off the tick thread now. provider_->loadChunk() is
        //    thread-safe (the Anvil provider serializes region access
        //    with io_mtx_ against its own writer thread); a provider
        //    without disk backing simply returns nullopt.
        if (provider_) {
            try {
                auto loaded = [&] {
                    performance::ScopedStage diskTimer(performance::Stage::DiskLoad);
                    return provider_->loadChunk(x, z);
                }();
                if (loaded && loaded.value()) {
                    auto* adapter = dynamic_cast<format::ChunkAdapter*>(loaded.value().get());
                    if (adapter && adapter->chunk()) {
                        mcp_chunk_t* raw = adapter->releaseChunk();
                        if (raw) {
                            install(raw); // callback owns pending_ now
                            return;
                        }
                    }
                }
                // loadChunk() had a value but we couldn't extract a
                // usable chunk from it (bad adapter / null release) —
                // fall through to generation below instead of leaving
                // this chunk stuck.
            } catch (...) {
                // A provider failure must not strand the request: fall
                // through to generation, which carries its own
                // emergency-chunk guarantee.
            }
        }

        // 2) Not on disk (or unusable) — generate it on this worker.
        generator::GenerationTask gen(generator_, x, z, seed, install);
        gen.run();
    });

    if (pool->submit([task] {
            (*task)();
        })) {
        // Successfully handed off — the task/install callback now owns
        // clearing `pending_` once the chunk is materialized. Tell the
        // guard not to erase it out from under the in-flight task.
        handed_to_pool = true;
    }
    // Pool refused (shutting down): handed_to_pool stays false, so the
    // guard erases pending_ on return — caller just sees nullptr this
    // tick and can retry later instead of the slot leaking forever.
    return nullptr;
}

void ChunkManager::setChunk(int32_t x, int32_t z, mcp_chunk_t* chunk) {
    std::vector<std::vector<uint8_t>> drainedTiles;
    std::vector<char> preBlobMatches;
    {
        const auto t0 = SteadyClock::now();
        std::scoped_lock lk(mtx_, pending_mtx_);
        noteLockWait(chunk_lock_, t0); // pair acquired as one event
        auto key = ChunkKey{x, z};
        auto it = chunks_.find(key);
        if (it != chunks_.end() && it->second && it->second != chunk)
            mcp_chunk_destroy(it->second);
        chunks_[key] = chunk;
        count_.store(chunks_.size(), std::memory_order_relaxed);
        // Snapshot before the replay (see the async install for the
        // drop-vs-heal contract the replay feeds).
        preBlobMatches = snapshotTileBlobMatches(chunk);
        // Replay generation-time writes that were queued for this chunk
        // while it was absent (neighbors populated first). Done under the
        // same locks the generator's commitNeighborWrite() uses, so a
        // write can never land between "queued" and "live".
        if (spillQueue_)
            spillQueue_->drainSpills(x, z, chunk);
        // Structure tile blobs parked while the chunk was absent land in
        // its tile_entities here, before any client can stream it — each
        // one only while its block survived the spill replay above.
        takePendingTileBlobs(key, drainedTiles);
        keepBlockMatchedTileBlobs(chunk, drainedTiles);
        reconcileStoredTileBlobs(chunk, preBlobMatches);
    }
    // Registry tiles for the drained blobs — outside the locks, same
    // contract as on_chunk_added_ below.
    if (on_tile_blob_) {
        for (const auto& blob : drainedTiles)
            on_tile_blob_(blob.data(), blob.size());
    }
    // Global sky light is a property of the engine, not of the chunk's
    // history: chunks coming back from a disk written before the
    // global-light switch carry the old per-column shadow arrays, so
    // heal them on every install. Idempotent (memset of allocated
    // sections only) and never marks the chunk dirty.
    mcp_chunk_fill_sky_light(chunk);
    // Outside the lock: the callback (World) only enqueues the chunk for a
    // later light sweep. Calling it with mtx_ held would deadlock on the
    // very mutex ChunkManager needs.
    //
    // BORDER, not FULL: the chunk arrives with its interior already lit
    // (worker pass on generation, stored light on a disk load), so the
    // exact re-flood would only re-derive values the chunk already has
    // and re-wipe 15 blocks into each healthy neighbour. The border pass
    // pulls/pushes the cross-border flow and flags/queues whatever the
    // spill replay (drainSpills above) actually changed.
    if (on_chunk_added_)
        on_chunk_added_(x, z, /*full=*/false);
    if (on_chunk_installed_)
        on_chunk_installed_(x, z);
}

void ChunkManager::commitNeighborWrite(int cx, int cz, uint32_t index, uint8_t id, uint8_t data) {
    const auto t0 = SteadyClock::now();
    mcp_chunk_t* live = nullptr;
    bool changed = false;
    {
        std::scoped_lock lk(mtx_, pending_mtx_);
        const uint64_t waitNs =
            (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(SteadyClock::now() - t0)
                .count();
        noteLockWait(chunk_lock_, t0);      // pair acquired as one event
        spill_lock_wait_ns_.fetch_add(waitNs, std::memory_order_relaxed);
        spill_writes_.fetch_add(1, std::memory_order_relaxed);
        auto it = chunks_.find(ChunkKey{cx, cz});
        if (it != chunks_.end() && it->second) {
            live = it->second;
        } else if (spillQueue_) {
            // Chunk not live yet: park the write for its populate/install.
            // Holding both locks here makes the queue-vs-live decision
            // atomic against the installer (drainSpills in setChunk and in
            // the async callback), so the write cannot be swallowed.
            spill_parked_.fetch_add(1, std::memory_order_relaxed);
            spillQueue_->queueSpill(cx, cz, index, id, data);
        }
        if (live) {
            uint32_t y = index % 128;
            uint32_t rest = index / 128;
            uint32_t z = rest % 16;
            uint32_t x = rest / 16;
            // PERF (light storm during population): features spill whole
            // volumes — a lake writes its full 16x16x8 envelope even
            // where the target cell already holds the same id/data. Every
            // surviving write used to re-queue a full 46x46 relight for
            // the chunk. A spill that writes the exact bytes the live
            // chunk already holds changes nothing observable, so skip the
            // write AND the relight entirely (the chunk still gets one
            // relight per genuinely-changed cell batch below).
            if (mcp_chunk_get_block_id(live, (int)x, (int)y, (int)z) != id ||
                mcp_chunk_get_block_data(live, (int)x, (int)y, (int)z) != data) {
                // Block↔tile invariant at the spill seam: a generation
                // write that replaces a block-entity block with something
                // else kills that block's tile exactly like the vanilla
                // block-removal path would — the actor dies with its
                // block, never survives over foreign terrain.
                const uint8_t oldId = mcp_chunk_get_block_id(live, (int)x, (int)y, (int)z);
                mcp_chunk_set_block_full(live, (int)x, (int)y, (int)z, id, data);
                changed = true;
                if (on_spill_tile_block_overwrite_ &&
                    cursestone::tile::isTileBlock(oldId) && oldId != id) {
                    on_spill_tile_block_overwrite_(cx * 16 + (int)x, (int)y, cz * 16 + (int)z);
                }
            }
        }
    }
    // The new blocks change the sky light the same way a player edit
    // would — requeue the light sweep for the chunk, outside the locks
    // (same contract as setChunk's on_chunk_added_). This one is FULL:
    // blocks genuinely changed under a live chunk, so the exact
    // clear-and-reflood sweep (chunk + one light range into every
    // neighbour) is required to darken what the new blocks shadow.
    if (changed && on_chunk_added_)
        on_chunk_added_(cx, cz, /*full=*/true);
}

void ChunkManager::commitNeighborTile(int cx, int cz, const std::vector<uint8_t>& blob) {
    if (blob.empty())
        return;
    const auto t0 = SteadyClock::now();
    bool landedLive = false;
    {
        std::scoped_lock lk(mtx_, pending_mtx_);
        noteLockWait(chunk_lock_, t0); // pair acquired as one event
        auto it = chunks_.find(ChunkKey{cx, cz});
        if (it != chunks_.end() && it->second) {
            // Live chunk: the blob goes straight into tile_entities —
            // exactly where the block write next door just landed.
            // Block↔tile invariant: only when the block actually stands.
            // The blob routes here microseconds after its block write, but
            // a concurrent spill for the same cell can legally have won —
            // the tile dies with its block, vanilla-style.
            mcp_chunk_t* live = it->second;
            std::string tileId;
            int32_t bx = 0, by = 0, bz = 0;
            const uint8_t expected =
                cursestone::tile::parseTileBlobHeader(blob.data(), blob.size(), tileId, bx, by, bz)
                    ? cursestone::tile::blockIdForTileId(tileId)
                    : uint8_t{0};
            const bool usable = expected != 0 && by >= 0 && by < 128 &&
                                mcp_chunk_get_block_id(live, bx & 15, by, bz & 15) == expected;
            if (usable) {
                mcp_chunk_add_tile_entity(live, blob.data(), blob.size());
                landedLive = true;
            }
        } else {
            // Chunk not live yet: park the blob for its install (setChunk
            // / async callback), beside the block spills for the same
            // chunk — the pair can never be split by an install in
            // between because both decisions share these locks. The
            // install re-checks the block before the blob is added.
            pendingTileBlobs_[ChunkKey{cx, cz}].push_back(blob);
        }
    }
    // Live chunk: also create the registry tile right away — the save
    // pass rebuilds tile_entities from the registry, so a blob that only
    // lives in the chunk could be wiped by the next dirty-tile sync
    // before the first hydration reads it. applyTileBlob inserts
    // only-if-free, so a tile hydrated in the meantime wins.
    if (landedLive && on_tile_blob_)
        on_tile_blob_(blob.data(), blob.size());
}

void ChunkManager::takePendingTileBlobs(const ChunkKey& key,
                                        std::vector<std::vector<uint8_t>>& out) {
    auto it = pendingTileBlobs_.find(key);
    if (it == pendingTileBlobs_.end())
        return;
    for (auto& blob : it->second)
        out.push_back(std::move(blob));
    pendingTileBlobs_.erase(it);
}

// Adds the drained structure-tile blobs to `chunk`'s tile_entities, but
// only those whose block still stands there: spills replayed a moment
// earlier (drainSpills) can legally have wiped the tile-block the blob
// describes — a lake refill, a later dungeon attempt's interior clear —
// and a tile that outlives its block is exactly the invisible-chest bug
// (the 0.15 client draws the chest through its block actor, which only
// binds over a matching block). Survivors stay in `blobs` so the caller's
// on_tile_blob_ pass materializes only what was actually added.
void ChunkManager::keepBlockMatchedTileBlobs(mcp_chunk_t* chunk,
                                             std::vector<std::vector<uint8_t>>& blobs) {
    if (!chunk || blobs.empty())
        return;
    size_t out = 0;
    for (size_t i = 0; i < blobs.size(); ++i) {
        auto& blob = blobs[i];
        std::string tileId;
        int32_t bx = 0, by = 0, bz = 0;
        const uint8_t expected =
            cursestone::tile::parseTileBlobHeader(blob.data(), blob.size(), tileId, bx, by, bz)
                ? cursestone::tile::blockIdForTileId(tileId)
                : uint8_t{0};
        const bool usable = expected != 0 && by >= 0 && by < 128 &&
                            mcp_chunk_get_block_id(chunk, bx & 15, by, bz & 15) == expected;
        if (!usable)
            continue;
        mcp_chunk_add_tile_entity(chunk, blob.data(), blob.size());
        if (out != i)
            blobs[out] = std::move(blob);
        ++out;
    }
    blobs.resize(out);
}

// Per-blob "its matching block stands" snapshot of the tile blobs the
// chunk carries at this instant (see reconcileStoredTileBlobs).
std::vector<char> ChunkManager::snapshotTileBlobMatches(mcp_chunk_t* chunk) const {
    std::vector<char> out;
    if (!chunk || chunk->tile_entity_count == 0)
        return out;
    out.reserve(chunk->tile_entity_count);
    for (size_t i = 0; i < chunk->tile_entity_count; ++i) {
        const uint8_t* blob = chunk->tile_entities[i];
        const size_t blen = chunk->tile_entity_lens ? chunk->tile_entity_lens[i] : 0;
        std::string tileId;
        int32_t bx = 0, by = 0, bz = 0;
        const uint8_t expected =
            cursestone::tile::parseTileBlobHeader(blob, blen, tileId, bx, by, bz)
                ? cursestone::tile::blockIdForTileId(tileId)
                : uint8_t{0};
        const bool matched = expected != 0 && by >= 0 && by < 128 &&
                             mcp_chunk_get_block_id(chunk, bx & 15, by, bz & 15) == expected;
        out.push_back(matched ? 1 : 0);
    }
    return out;
}

// Reconciles the tile blobs `chunk` carries against their blocks after the
// spill replay. `matchedBefore` snapshots, per pre-existing blob, whether
// its block stood BEFORE the replay:
//   - a blob whose block stood before and is gone now was killed by a
//     parked spill of this very install (lake refill, later structure
//     pass) — vanilla destroys the block actor with its block, so the
//     blob is dropped;
//   - a blob that arrived from disk already broken is legacy damage from
//     builds whose generation could stomp a chest block while its tile
//     survived — the block is healed back (facing rides the tile
//     compound when it has one) so the saved chest comes back instead of
//     staying the invisible-chest shape.
// Blobs appended after the snapshot (keepBlockMatchedTileBlobs) were
// already checked against the post-replay blocks and are left alone.
void ChunkManager::reconcileStoredTileBlobs(mcp_chunk_t* chunk,
                                            const std::vector<char>& matchedBefore) {
    if (!chunk)
        return;
    const size_t preCount = std::min(matchedBefore.size(), chunk->tile_entity_count);
    for (size_t i = 0; i < preCount;) {
        const uint8_t* blob = chunk->tile_entities[i];
        const size_t blen = chunk->tile_entity_lens ? chunk->tile_entity_lens[i] : 0;
        std::string tileId;
        int32_t bx = 0, by = 0, bz = 0;
        const uint8_t expected =
            cursestone::tile::parseTileBlobHeader(blob, blen, tileId, bx, by, bz)
                ? cursestone::tile::blockIdForTileId(tileId)
                : uint8_t{0};
        if (expected == 0 || by < 0 || by >= 128) {
            mcp_chunk_remove_tile_entity_at(chunk, i);
            continue;
        }
        const uint8_t at = mcp_chunk_get_block_id(chunk, bx & 15, by, bz & 15);
        if (at == expected) {
            ++i;
            continue;
        }
        if (matchedBefore[i]) {
            // The spill replay of this install killed the block — the
            // tile dies with it (vanilla rule).
            mcp_chunk_remove_tile_entity_at(chunk, i);
            continue;
        }
        // Legacy damage: the blob was saved without its block. Heal the
        // block back so the chest stands and opens again.
        uint8_t data = 0;
        if (expected == (uint8_t)cursestone::block::CHEST) {
            // facing rides the compound for structure chests; 2 = north
            // for blobs saved before the heal existed.
            int32_t facing = 2;
            {
                cursestone::utils::TagGuard tag(
                    cursestone::nbt::read(blob, blen, /*little_endian=*/true));
                if (tag) {
                    int32_t stored = cursestone::nbt::get_int(tag.get(), "facing", 2);
                    if (stored >= 2 && stored <= 5)
                        facing = stored;
                }
            }
            data = (uint8_t)facing;
        }
        mcp_chunk_set_block_full(chunk, bx & 15, by, bz & 15, expected, data);
        mcp_chunk_recalculate_height_map(chunk);
        chunk->dirty = true; // the healed pair persists too
        ++i;
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
