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
// ChunkManager — caches chunks in memory and routes load/save
// requests to the active WorldProvider. Mirrors the reference server's
// ChunkManager role. The World owns one of these. Generators run
// on the AsyncPool and post completed chunks back via setChunk().
#pragma once
#include "world/format/Chunk.h"
#include "world/format/WorldProvider.h"
#include "world/generator/NeighborChunkIO.h"
#include <atomic>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <mutex>
#include <vector>
#include <utility>
namespace cursestone::world::generator {
class Generator;
}
namespace cursestone::scheduler {
class AsyncPool;
}

namespace cursestone::world {

class ChunkManager : public generator::INeighborBlockSink {
  public:
    // Lock contention counters (relaxed atomics, pure observers): wait-to-
    // acquire time and acquisition counts for the two chunk-store mutexes.
    // The spawn-pregen scaling report diffs two snapshots; nothing here
    // influences behavior or ordering.
    struct LockStats {
        std::atomic<uint64_t> wait_ns{0};
        std::atomic<uint64_t> acquires{0};
        struct Snapshot {
            uint64_t wait_ns;
            uint64_t acquires;
        };
        Snapshot snap() const {
            return {wait_ns.load(std::memory_order_relaxed),
                    acquires.load(std::memory_order_relaxed)};
        }
    };

    ChunkManager();
    ~ChunkManager();
    mcp_chunk_t* getChunkIfLoaded(int32_t x, int32_t z) const;
    mcp_chunk_t* getChunk(int32_t x, int32_t z);

    // Non-blocking chunk fetch used by the per-player streaming path
    // (Player::onUpdate). Never generates OR loads from disk on the
    // calling thread:
    //   - if the chunk is already in memory, returns it immediately.
    //   - otherwise, if a request for (x,z) isn't already in flight,
    //     submits ONE pool task that tries the provider (disk) first and
    //     falls back to generate()+populate() on a miss (both on a worker
    //     thread, per Generator's "stateless w.r.t. main thread"
    //     contract), and returns nullptr right away.
    //   - if a request IS already in flight, just returns nullptr
    //     again (no duplicate work submitted).
    // The caller should treat nullptr as "not ready yet, try again
    // next tick" rather than as a permanent miss.
    //
    // PERF (TPS 19 during exploration): the disk half used to run on the
    // calling (main) thread under a "cheap enough" assumption that does
    // not hold on real storage — each loadChunk() is region open + read
    // + zlib inflate + NBT decode, and a player crossing fresh terrain
    // requests up to 4 chunks per tick. That put multi-millisecond disk
    // stalls straight into the tick; over a one-second TPS window the
    // accumulated overruns showed up as a lost tick. Loading now rides
    // the same worker task generation already uses, at the cost of the
    // chunk arriving one tick later (the stream loop re-requests and
    // sends the next tick anyway).
    mcp_chunk_t* getChunkOrRequestAsync(int32_t x, int32_t z, scheduler::AsyncPool* pool);

    void setChunk(int32_t x, int32_t z, mcp_chunk_t* chunk);
    // Called every time a chunk enters the manager (generated, loaded
    // from disk, or replaced). World installs a callback here so a chunk
    // that appears later than its neighbours still gets its lighting
    // swept together with them — without it a chunk keeps the lighting it
    // was generated with in isolation and shows a dark border where the
    // light should carry over. Must not block or touch ChunkManager:
    // setChunk() runs on the generation worker thread.
    //
    // `full` tells the engine which entry to queue: true = blocks changed
    // under the chunk since it was lit (generation spill batches land
    // here — the exact 46x46 sweep), false = the chunk merely arrived
    // with its worker-lit interior (the cheap border pass). A chunk
    // arrival must NOT pay the full sweep: with async workers the main
    // thread installs dozens of chunks per second during a flight, and
    // re-flooding chunk + 15 into every neighbour twice per tick is what
    // dropped the flight TPS and re-stained the light chunk by chunk.
    void setOnChunkAdded(std::function<void(int32_t, int32_t, bool)> cb) {
        on_chunk_added_ = std::move(cb);
    }
    // Fired once per chunk that enters the level through ANY install
    // path — disk load, synchronous generation, async generation — after
    // every lock is released (same rules as setOnChunkAdded). World uses
    // it to queue the dynamic-liquid wake scan: the reference engine
    // fills each chunk's block ticking queue with the flowing water and
    // lava the generator wrote, and that queue starts running the moment
    // the chunk goes live, which is what makes generated springs spill.
    void setOnChunkInstalled(std::function<void(int32_t, int32_t)> cb) {
        on_chunk_installed_ = std::move(cb);
    }
    void markDirty(int32_t x, int32_t z);
    // Escribe TODO lo sucio de golpe, con mtx_ cogido de principio a fin.
    // Bloquea al que llama (y a los workers de generacion que esperan ese
    // lock) hasta que termina. Solo para las rutas que necesitan el mundo
    // entero en disco: apagado / saveAll() / comando /save.
    int flushDirty();
    // Igual que flushDirty() pero con PRESUPUESTO: escribe como maximo
    // `max_chunks` chunks sucios y se corta antes si ya consumio
    // `max_ms` milisegundos. Devuelve cuantos escribio.
    //
    // Es la version que usa el autosave: llamandola cada tick, el coste
    // de guardar el mundo queda repartido entre varios ticks en vez de
    // caer como un pico unico en el tick loop. Medido antes de este
    // cambio, con chunks de terreno real (generador Normal): 400 chunks
    // sucios = ~300 ms de una sola vez (y 759 ms en el caso reportado,
    // con el mundo mas cargado, ~1000 chunks) — eso es el congelamiento
    // que el cliente acaba leyendo como desconexion.
    int flushDirtyBudget(size_t max_chunks, uint64_t max_ms);
    // Frees a single chunk's memory. If `save` is true and a provider
    // is set, the chunk is saved first (only if it's actually dirty —
    // an unmodified chunk doesn't need a disk write just because it's
    // being evicted). Returns false if the chunk wasn't loaded.
    //
    // NOTE: this does NOT touch tile-entities living in that chunk —
    // World::unloadDistantChunks() calls World::evictTilesForChunk()
    // first to flush/drop them, because ChunkManager has no visibility
    // into World::tiles_ (tile-entity registry lives one layer up).
    // Calling this directly without doing that first would still work
    // (the chunk's mcp_chunk_t::tile_entities blob only gets rewritten
    // by World::syncTilesToChunks()/evictTilesForChunk() in the first
    // place), but any tile whose dirty NBT was never flushed into the
    // chunk stays unsaved, and World::tiles_ would go on holding a Tile
    // object for a chunk that's no longer in memory.
    bool unloadChunk(int32_t x, int32_t z, bool save = true);
    // Coordinates of every chunk currently held in memory. The caller
    // (World::unloadDistantChunks()) uses this to decide what's
    // eligible for eviction. Returns a snapshot copy rather than a
    // reference so the caller can iterate it without holding mtx_ for
    // the whole sweep (each unloadChunk() call above takes the lock
    // itself, per-chunk).
    std::vector<std::pair<int32_t, int32_t>> loadedChunkCoords() const;
    // Numero de chunks en memoria con cambios sin escribir. El autosave
    // lo usa para saber si tiene que seguir drenando el flush.
    size_t dirtyCount() const;
    // Number of chunk-generation requests currently in flight. unloadWorld
    // waits on this before destroying the world: the completion callback
    // runs on a worker thread and captures this ChunkManager.
    size_t pendingCount() const {
        std::lock_guard<std::mutex> lk(pending_mtx_);
        return pending_.size();
    }
    void setProvider(std::shared_ptr<format::WorldProvider> p) {
        provider_ = std::move(p);
    }
    std::shared_ptr<format::WorldProvider> provider() const {
        return provider_;
    }
    // Set the world generator used to create new chunks when they're
    // not on disk. Mirrors the era reference where the generator is queried
    // for terrain + population.
    void setGenerator(std::shared_ptr<generator::Generator> g) {
        generator_ = std::move(g);
    }
    // Wire the generator's spill queue. Neighbor-aimed generation writes
    // for chunks that are not live yet are parked there and replayed the
    // moment the chunk becomes live (populate or install).
    void setSpillQueue(generator::ISpillQueue* q) {
        spillQueue_ = q;
    }
    // generator::INeighborBlockSink — target of the generator's
    // cross-chunk block writes (trees/lakes/structures that spill over a
    // chunk border).
    void commitNeighborWrite(int cx, int cz, uint32_t index, uint8_t id, uint8_t data) override;
    // Same for tile-entity NBT blobs (structure chests): live chunk gets
    // the blob added and handed to the blob sink below; not-yet-live
    // chunks get it parked until install, exactly like the block spills.
    void commitNeighborTile(int cx, int cz, const std::vector<uint8_t>& blob) override;
    // Wire the callback applied to a tile blob that landed in a LIVE
    // chunk (or was drained on install). World registers its registry
    // apply here so the chest's tile exists beside its block instead of
    // waiting for the next hydration pass — a save pass that rebuilds
    // tile_entities from the registry between the two moments would
    // otherwise wipe a blob-only chest.
    void setTileBlobSink(std::function<void(const uint8_t*, size_t)> cb) {
        on_tile_blob_ = std::move(cb);
    }
    // Wire the callback applied when a generation spill overwrites a
    // block-entity block in a LIVE chunk with a different block. World
    // registers its registry removal here so the dead block's tile dies
    // with it (the vanilla rule) instead of surviving as a block actor
    // over foreign terrain — the invisible-chest shape.
    void setOnSpillTileBlockOverwrite(std::function<void(int32_t, int32_t, int32_t)> cb) {
        on_spill_tile_block_overwrite_ = std::move(cb);
    }
    size_t count() const;

    // Contention counters for the pregen scaling report (drain-style:
    // subtract two snapshots to get a window's numbers).
    LockStats::Snapshot chunkLockSnap() const { return chunk_lock_.snap(); }
    LockStats::Snapshot pendingLockSnap() const { return pending_lock_.snap(); }
    uint64_t spillWritesSnap() const { return spill_writes_.load(std::memory_order_relaxed); }
    uint64_t spillParkedSnap() const { return spill_parked_.load(std::memory_order_relaxed); }
    uint64_t spillLockWaitNsSnap() const { return spill_lock_wait_ns_.load(std::memory_order_relaxed); }

  private:
    struct ChunkKey {
        int32_t x, z;
        bool operator==(const ChunkKey& o) const {
            return x == o.x && z == o.z;
        }
    };
    struct ChunkKeyHash {
        size_t operator()(const ChunkKey& k) const noexcept {
            return std::hash<int64_t>()((int64_t)k.x * 0x9e3779b97f4a7c15LL ^ (int64_t)k.z);
        }
    };
    mutable std::mutex mtx_;
    std::function<void(int32_t, int32_t, bool)> on_chunk_added_;
    std::function<void(int32_t, int32_t)> on_chunk_installed_;
    std::unordered_map<ChunkKey, mcp_chunk_t*, ChunkKeyHash> chunks_;
    // Mirrors chunks_.size(); written under mtx_, read lock-free by count().
    std::atomic<size_t> count_{0};
    std::shared_ptr<format::WorldProvider> provider_;
    std::shared_ptr<generator::Generator> generator_;
    generator::ISpillQueue* spillQueue_ = nullptr;

    // Tile blobs parked for chunks that were not live yet (structure
    // chests spilled into a chunk that generated later) — drained on
    // install right after the block spills, under the same locks.
    std::unordered_map<ChunkKey, std::vector<std::vector<uint8_t>>, ChunkKeyHash> pendingTileBlobs_;
    std::function<void(const uint8_t*, size_t)> on_tile_blob_;
    std::function<void(int32_t, int32_t, int32_t)> on_spill_tile_block_overwrite_;

    // Pops every parked blob for `key` into `out` (call with mtx_ +
    // pending_mtx_ held). Callers hand the blobs to the chunk and then to
    // on_tile_blob_ outside the locks (same contract as on_chunk_added_).
    void takePendingTileBlobs(const ChunkKey& key,
                              std::vector<std::vector<uint8_t>>& out);

    // Adds drained structure-tile blobs to `chunk`'s tile_entities but
    // keeps only the ones whose matching block still stands there (spill
    // replays can legally have wiped it); survivors stay in `blobs` so
    // the caller's on_tile_blob_ pass sees exactly what was added.
    void keepBlockMatchedTileBlobs(mcp_chunk_t* chunk,
                                   std::vector<std::vector<uint8_t>>& blobs);

    // Per-blob "its matching block stands" snapshot taken BEFORE the spill
    // replay (see reconcileStoredTileBlobs for the drop-vs-heal contract).
    std::vector<char> snapshotTileBlobMatches(mcp_chunk_t* chunk) const;

    // Reconciles the tile blobs `chunk` carries against their blocks after
    // the spill replay: blobs whose block the replay killed are dropped
    // (vanilla rule), blobs already broken on disk get their block healed
    // back (legacy-damage migration).
    void reconcileStoredTileBlobs(mcp_chunk_t* chunk,
                                  const std::vector<char>& matchedBefore);

    // Coordinates currently being generated on an AsyncPool worker,
    // so getChunkOrRequestAsync() doesn't submit the same chunk twice
    // while it's still in flight. Guarded by pending_mtx_ (separate
    // from mtx_ since the worker thread touches chunks_ via setChunk()
    // — which takes mtx_ itself — right before it clears this set).
    mutable std::mutex pending_mtx_;
    std::unordered_set<ChunkKey, ChunkKeyHash> pending_;

    // Contention observers for the two store mutexes and the neighbor-
    // spill path (see LockStats above).
    mutable LockStats chunk_lock_;
    mutable LockStats pending_lock_;
    std::atomic<uint64_t> spill_writes_{0};       // cross-chunk writes seen
    std::atomic<uint64_t> spill_parked_{0};       // writes parked (chunk not live)
    std::atomic<uint64_t> spill_lock_wait_ns_{0}; // wait on the spill decision lock
};
} // namespace
