/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
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
// LevelDB — MCPE's default on-disk format, on CurseStone's own engine.
//
// A LevelDB world looks exactly like one written by the real 0.15.x device:
//
//   worlds/<name>/
//     db/CURRENT          engine manifest pointer
//     db/LOCK             flock'ed while the world is open
//     db/MANIFEST-<n>     table set + WAL number + last sequence
//     db/<n>.log          WAL (write-ahead log) of unflushed batches
//     db/<n>.ldb          SSTables holding the chunk records
//     level.dat           [v3][size][little-endian NBT] world metadata
//     level.dat_old       previous level.dat (rotated on every save)
//     levelname.txt       human-readable world name
//
// Reads accept worlds written by the real device (snappy tables) and by
// PocketMine (zlib tables); writes emit zlib level-1 table blocks — valid
// everywhere. Chunks ride the legacy "0"/"f"/"v"/"1"/"2" record family
// (see LevelDBChunkCodec.h).
//
// Threading mirrors the Anvil provider: saveChunk() snapshots the chunk on
// the main tick thread (a memcpy of the arrays the serializer reads) and a
// single background worker transposes + batches it into the engine, so a
// large autosave never stalls a tick. The engine auto-flushes its memtable
// to a table every ~4 MiB and compacts when level-0 tables pile up.
#pragma once

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "../../../CurseStone.h"
#include "../WorldProvider.h"
#include "../Chunk.h" // the wire snapshot the save pipeline snapshots
#include "DBEngine.h"

namespace cursestone::nbt {
class Tag;
}

namespace cursestone::world::format::leveldb {

class LevelDB : public WorldProvider {
  public:
    LevelDB();
    ~LevelDB() override;

    // ----- WorldProvider
    void init(const std::string& path) override;
    void shutdown() override;
    std::string getName() const override {
        return "leveldb";
    }

    std::optional<std::shared_ptr<FullChunk>> loadChunk(int32_t x, int32_t z) override;
    void saveChunk(const std::shared_ptr<FullChunk>& chunk) override;
    void unloadChunk(int32_t x, int32_t z) override;

    std::vector<std::pair<int32_t, int32_t>> listChunks() const override;

    int64_t getSeed() const override {
        return seed_;
    }
    void setSeed(int64_t s) override;
    int64_t getCurrentTime() const override {
        return time_;
    }
    void setCurrentTime(int64_t t) override;
    math::Vector3 getSpawn() const override {
        return spawn_;
    }
    void setSpawn(const math::Vector3& v) override;

    void setGenerator(generator::Generator* g) override {
        generator_ = g;
    }
    generator::Generator* getGenerator() const override {
        return generator_;
    }

    // Rotates level.dat -> level.dat_old and writes the fresh one; keeps
    // levelname.txt in step with the level name.
    void saveLevelData() override;

    // Blocks until every chunk write accepted by saveChunk() reached the
    // engine (and the engine's memtable reached disk). Save-all / shutdown.
    void flushPendingSaves() override;

    // Path to the database directory this provider maintains.
    const std::string& dbPath() const {
        return db_path_;
    }

  private:
    // Parses the stored level.dat into seed_/time_/spawn_/names and keeps
    // the full tag tree for a lossless re-save. Builds the default tree of
    // a fresh world when no level.dat exists (creation).
    void loadMetadata();
    // Mutates the owned metadata tree with the current runtime values.
    void applyMetadata();
    // Setters land here: a freshly created world writes level.dat right
    // away so the creation-time file set is complete (seed/spawn included);
    // an existing world defers to the next saveLevelData().
    void onMetadataChanged();
    // True when this boot CREATED the world (no level.dat existed at init).
    bool fresh_world_ = false;

    // The world name = last path component of the world directory.
    std::string directoryName() const;
    void refreshLevelnameFile() const;

    // Async save pipeline (same shape as Anvil's).
    struct SaveJob {
        int32_t x = 0;
        int32_t z = 0;
        mcp_chunk_wire_snapshot_t* snapshot = nullptr; // owned, freed by worker
        std::vector<uint8_t> tiles;   // concatenated LE compound blobs
        std::vector<uint8_t> entities;
        uint8_t flags = 0;
    };
    void startWorker();
    void stopWorker(); // drains the queue, joins the thread
    void workerLoop();
    void enqueueSave(SaveJob&& job);

    std::string path_;
    std::string db_path_;
    DBEngine engine_;

    // Metadata (level.dat) as a live tag tree — re-saved whole so fields a
    // device wrote but CurseStone doesn't manage survive the round trip.
    nbt::Tag* meta_root_ = nullptr; // owned
    int64_t seed_ = 0;
    int64_t time_ = 0;
    math::Vector3 spawn_{0.0, 64.0, 0.0};
    std::string level_name_;
    std::string generator_name_ = "normal";
    generator::Generator* generator_ = nullptr;

    std::thread worker_;
    std::mutex job_mtx_;
    std::condition_variable job_cv_;
    std::condition_variable done_cv_;
    std::deque<SaveJob> job_queue_;
    bool job_busy_ = false;
    bool worker_stop_ = false;
    // RAM the queue currently holds (snapshot + tile/entity blob bytes,
    // maintained under job_mtx_ on push/pop). /status reads the gauge
    // through SaveProfiler; the ceiling below bounds this to ~45 MB.
    size_t queued_bytes_ = 0;
    static size_t job_bytes(const SaveJob& j) {
        size_t b = j.tiles.size() + j.entities.size();
        // mcp_chunk_wire_snapshot_t worst case: 8 sections of
        // (4096 blocks + 2048 data + 2048 sky + 2048 light) + biomes
        // (256) + heightmap (256 x int32) + header fields.
        return b + 8u * (4096u + 2048u * 3u) + 256u + 256u * 4u + 64u;
    }
    // ~83 KB per snapshot; 512 in flight is a ~45 MB ceiling, never hit in
    // practice — the cap only guards a wedged disk.
    static constexpr size_t kMaxQueuedSaves = 512;
};

struct LevelDBRegistrar {
    LevelDBRegistrar();
};
extern LevelDBRegistrar g_leveldb_registrar_;

} // namespace cursestone::world::format::leveldb
