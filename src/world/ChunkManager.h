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
// ChunkManager — caches chunks in memory and routes load/save
// requests to the active WorldProvider. Mirrors the reference server's
// ChunkManager role. The World owns one of these. Generators run
// on the AsyncPool and post completed chunks back via setChunk().
#pragma once
#include "world/format/Chunk.h"
#include "world/format/WorldProvider.h"
#include "world/generator/NeighborChunkIO.h"
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
    // from disk, or replaced). World installs a callback here so a chunk
    // that appears later than its neighbours still gets its lighting
    // swept together with them — without it a chunk keeps the lighting it
    // was generated with in isolation and shows a dark border where the
    // light should carry over. Must not block or touch ChunkManager:
    // setChunk() runs on the generation worker thread.
    void setOnChunkAdded(std::function<void(int32_t, int32_t)> cb) {
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
    // not on disk. Mirrors Nukkit where the generator is queried
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
    size_t count() const;

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
    std::function<void(int32_t, int32_t)> on_chunk_added_;
    std::function<void(int32_t, int32_t)> on_chunk_installed_;
    std::unordered_map<ChunkKey, mcp_chunk_t*, ChunkKeyHash> chunks_;
    std::shared_ptr<format::WorldProvider> provider_;
    std::shared_ptr<generator::Generator> generator_;
    generator::ISpillQueue* spillQueue_ = nullptr;

    // Coordinates currently being generated on an AsyncPool worker,
    // so getChunkOrRequestAsync() doesn't submit the same chunk twice
    // while it's still in flight. Guarded by pending_mtx_ (separate
    // from mtx_ since the worker thread touches chunks_ via setChunk()
    // — which takes mtx_ itself — right before it clears this set).
    mutable std::mutex pending_mtx_;
    std::unordered_set<ChunkKey, ChunkKeyHash> pending_;
};
} // namespace
