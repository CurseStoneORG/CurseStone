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
// Anvil.h — Nukkit Anvil WorldProvider, ported to CurseStone's C++ core.
//
// This is the on-disk world format used by Nukkit InfinityEdition:
//   worlds/<name>/level.dat      (GZIP-compressed big-endian NBT, root → "Data")
//   worlds/<name>/region/r.X.Z.mca  (region files, 32x32 chunks each)
//
// The format is auto-detected by `isValid(path)` (level.dat present +
// region/ directory + every region file ends in `.mca`).
//
// Chunk loading flow:
//   loadChunk(x, z) → regionFor(x, z) → RegionLoader.readChunk()
//     → ZLIB inflate → anvil_chunk_decode() → ChunkAdapter.
//
// World creation (generate):
//   init(path) creates region/ if missing and writes a default level.dat
//   if one doesn't exist, mirroring Nukkit's Anvil.generate() defaults:
//     Data.{LevelName, RandomSeed, SpawnX/Y/Z, generatorName, version=19133, ...}
#pragma once

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "../../../CurseStone.h"
#include "../WorldProvider.h"
#include "AnvilRegionLoader.h"

namespace cursestone::world::format::anvil {

class Anvil : public WorldProvider {
  public:
    Anvil();
    ~Anvil() override;

    // ----- WorldProvider
    void init(const std::string& path) override;
    void shutdown() override;
    std::string getName() const override {
        return "anvil";
    }

    std::optional<std::shared_ptr<FullChunk>> loadChunk(int32_t x, int32_t z) override;
    void saveChunk(const std::shared_ptr<FullChunk>& chunk) override;
    void unloadChunk(int32_t x, int32_t z) override;

    std::vector<std::pair<int32_t, int32_t>> listChunks() const override;

    int64_t getSeed() const override {
        return seed_;
    }
    void setSeed(int64_t s) override {
        seed_ = s;
    }
    int64_t getCurrentTime() const override {
        return time_;
    }
    void setCurrentTime(int64_t t) override {
        time_ = t;
    }
    math::Vector3 getSpawn() const override {
        return spawn_;
    }
    void setSpawn(const math::Vector3& v) override {
        spawn_ = v;
    }

    void setGenerator(generator::Generator* g) override {
        generator_ = g;
    }
    generator::Generator* getGenerator() const override {
        return generator_;
    }

    // Flushes level.dat to disk (GZIP + big-endian NBT).
    void saveLevelData() override {
        saveMetadata();
    }

    // Blocks the caller until every chunk write accepted by saveChunk()
    // has been written to its region file. Used by the save-all and
    // shutdown paths (World::save) — the live autosave never calls this,
    // that's the whole point of the background writer.
    void flushPendingSaves() override;

    // ----- Anvil-specific surface

    // Path to the .mca region file for chunk (cx, cz):
    //   "<worldDir>/region/r.<rx>.<rz>.mca"
    std::string regionPath(int32_t cx, int32_t cz) const;

    // Get-or-open the RegionLoader for the region containing (cx, cz).
    // Returns nullptr on I/O error. `create` controls whether a new
    // .mca file is created if it doesn't exist (used by saveChunk).
    AnvilRegionLoader* regionFor(int32_t cx, int32_t cz, bool create);

    // Static detection — used by future auto-detect logic. Returns true
    // if `path` looks like an Anvil world (level.dat present, region/
    // dir exists, all region files end in .mca).
    static bool isValid(const std::string& path);

    // Static generator — creates the on-disk scaffolding for a fresh
    // Anvil world at `path` (writes level.dat with NBT Data compound +
    // creates region/ folder). Mirrors Nukkit's Anvil.generate().
    static bool generate(const std::string& path, const std::string& name, int64_t seed,
                         const std::string& generatorName);

  private:
    // Load level.dat (GZIP-compressed big-endian) → sets seed_/time_/spawn_.
    void loadMetadata();
    // Save level.dat with current metadata.
    void saveMetadata();
    // Evict the least-recently-used region file from `regions_`.
    // Caller must already hold io_mtx_.
    void evictOldestRegion();

    // ------------------------------------------------------------------
    // Async chunk writer — the PocketMine-style autosave pipeline.
    //
    // saveChunk() runs on the MAIN thread but only does the part that
    // must be there: anvil_chunk_encode() reads the live mcp_chunk_t
    // while the world is quiesced on that thread. The expensive part —
    // ZLIB compression and the region-file I/O — is handed to a single
    // background worker so a big autosave (hundreds of dirty chunks
    // after players explore new land) can never stall a tick.
    //
    // One worker, FIFO queue ⇒ two snapshots of the same chunk are
    // always written in enqueue order (older first), so a re-dirty
    // during a pending write can never end up overwritten by stale data.
    //
    // io_mtx_ serializes the region map (regions_/region_lru_ + the
    // FILE* handles inside AnvilRegionLoader) between the worker's
    // writes and the main thread's loadChunk()/eviction. Compression
    // happens OUTSIDE io_mtx_ — it never touches shared state.
    // ------------------------------------------------------------------
    struct SaveJob {
        int32_t x = 0;
        int32_t z = 0;
        std::vector<uint8_t> nbt; // big-endian NBT of the snapshot
    };

    void startWorker();
    void stopWorker(); // drains the queue, joins the thread
    void workerLoop();
    // Push one encoded chunk into the queue. Blocks (bounded) when the
    // queue is over capacity so a slow disk cannot balloon memory.
    void enqueueSave(SaveJob&& job);

    std::thread worker_;
    std::mutex job_mtx_;
    std::condition_variable job_cv_;  // wakes the worker
    std::condition_variable done_cv_; // wakes flushPendingSaves()
    std::deque<SaveJob> job_queue_;
    bool job_busy_ = false; // worker writing a job
    bool worker_stop_ = false;
    // Backpressure cap: raw NBT is ~53 KB/chunk, so 512 in flight is a
    // ~27 MB ceiling — never hit in practice (the worker drains far
    // faster than the main thread encodes), it only guards a wedged disk.
    static constexpr size_t kMaxQueuedSaves = 512;

    std::string path_;
    int64_t seed_ = 0;
    int64_t time_ = 0;
    math::Vector3 spawn_{0.0, 64.0, 0.0};
    std::string generator_name_ = "normal";
    generator::Generator* generator_ = nullptr;

    static constexpr size_t kMaxOpenRegions = 8;
    // io_mtx_ guards regions_ / region_lru_ and every FILE* operation in
    // the loaders — the worker thread writes while the main thread reads.
    mutable std::mutex io_mtx_;
    std::unordered_map<std::string, std::unique_ptr<AnvilRegionLoader>> regions_;
    std::vector<std::string> region_lru_;
};

struct AnvilRegistrar {
    AnvilRegistrar();
};
extern AnvilRegistrar g_anvil_registrar_;

} // namespace cursestone::world::format::anvil
