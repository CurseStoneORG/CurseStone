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
// LevelDB.cpp — the MCPE LevelDB WorldProvider, wired to CurseStone's own
// real-format engine (DBEngine) and the legacy record codec.
#include "LevelDB.h"
#include "DBFormat.h"
#include "LevelDBChunkCodec.h"
#include "../WorldProviderManager.h"
#include "../ChunkAdapter.h"
#include "../../../nbt/NBT.h"
#include "../../../nbt/tag/CompoundTag.h"
#include "../../../nbt/tag/Tag.h"
#include "../../../utils/MainLogger.hpp"
#include "../../../utils/ThreadPort.h"
#include "../../../utils/CRaii.h"
#include "../../../world/performance/SaveProfiler.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <chrono>

namespace cursestone::world::format::leveldb {

namespace {
// The engine's user keys are raw bytes (9 per chunk record); the codec
// builds them as byte vectors, so the provider bridges once, here.
std::string key_str(const std::vector<uint8_t>& key) {
    return std::string(key.begin(), key.end());
}
} // namespace

// ============================================================================
// Registrar — installs "leveldb" into WorldProviderManager at process start.
// ============================================================================
LevelDBRegistrar g_leveldb_registrar_;
LevelDBRegistrar::LevelDBRegistrar() {
    WorldProviderManager::getInstance().registerProvider("leveldb", []() {
        return std::unique_ptr<WorldProvider>(new LevelDB());
    });
}

LevelDB::LevelDB() = default;

LevelDB::~LevelDB() {
    shutdown();
}

// ============================================================================
// init — open the db, load (or create) level.dat.
// ============================================================================
void LevelDB::init(const std::string& path) {
    path_ = path;
    db_path_ = path_ + "/db";

    std::string err;
    if (!engine_.open(db_path_, &err)) {
        // Persistence is degraded, never fatal: the world keeps working
        // in memory and every accepted chunk write simply fails later.
        utils::MainLogger::get().error("[LevelDB] failed to open " + db_path_ + ": " + err);
    }

    loadMetadata();
    utils::MainLogger::get().debug("[LevelDB] Initialized at " + path_);
    startWorker();
}

void LevelDB::shutdown() {
    stopWorker(); // drains every queued chunk into the engine
    engine_.close(); // flushes the live memtable to a table
    saveLevelData();
    delete meta_root_;
    meta_root_ = nullptr;
}

// ============================================================================
// Metadata — level.dat ([v3][size][LE NBT]) with lossless round-trip.
// ============================================================================
std::string LevelDB::directoryName() const {
    if (path_.empty())
        return "world";
    size_t pos = path_.find_last_of('/');
    return (pos == std::string::npos) ? path_ : path_.substr(pos + 1);
}

void LevelDB::loadMetadata() {
    int32_t storage_version = 0;
    nbt::Tag* root = nullptr;
    if (LevelDBChunkCodec::read_level_dat(path_, &root, &storage_version)) {
        meta_root_ = root;
        using namespace cursestone::nbt;
        seed_ = get_long(meta_root_, "RandomSeed", 0);
        time_ = get_long(meta_root_, "Time", 0);
        int64_t day = get_long(meta_root_, "DayTime", 0);
        if (time_ == 0 && day != 0)
            time_ = day;
        int32_t sx = get_int(meta_root_, "SpawnX", 0);
        int32_t sy = get_int(meta_root_, "SpawnY", 64);
        int32_t sz = get_int(meta_root_, "SpawnZ", 0);
        spawn_ = math::Vector3{(double)sx, (double)sy, (double)sz};
        const char* lname = get_string(meta_root_, "LevelName", nullptr);
        level_name_ = (lname && *lname) ? lname : directoryName();
        const char* gen = get_string(meta_root_, "generatorName", nullptr);
        if (gen && *gen)
            generator_name_ = gen;
        utils::MainLogger::get().debug("[LevelDB] level.dat v" +
                                       std::to_string(storage_version) + " loaded: seed=" +
                                       std::to_string(seed_) + " spawn=" +
                                       std::to_string((int)spawn_.x) + "," +
                                       std::to_string((int)spawn_.y) + "," +
                                       std::to_string((int)spawn_.z));
        return;
    }

    // Fresh world: build the default metadata tree (the device field set,
    // same defaults PocketMine's generator wrote for 0.15 worlds) and put
    // the full file trio on disk immediately: level.dat, level.dat_old and
    // levelname.txt exist from the first moment a world is created.
    fresh_world_ = true;
    level_name_ = directoryName();
    utils::TagGuard fresh(new nbt::CompoundTag(""));
    if (!fresh)
        return;
    meta_root_ = fresh.release();
    applyMetadata();

    if (!LevelDBChunkCodec::write_level_dat(path_, meta_root_)) {
        utils::MainLogger::get().warn("[LevelDB] failed to create level.dat");
    } else {
        // The device keeps a backup of the last save beside level.dat; a
        // freshly created world carries its first one from birth.
        std::remove((path_ + "/level.dat_old").c_str());
        std::FILE* src = std::fopen((path_ + "/level.dat").c_str(), "rb");
        if (src) {
            std::vector<uint8_t> bytes;
            std::fseek(src, 0, SEEK_END);
            long sz = std::ftell(src);
            std::fseek(src, 0, SEEK_SET);
            if (sz > 0) {
                bytes.resize((size_t)sz);
                if (std::fread(bytes.data(), 1, bytes.size(), src) == bytes.size()) {
                    std::FILE* dst = std::fopen((path_ + "/level.dat_old").c_str(), "wb");
                    if (dst) {
                        std::fwrite(bytes.data(), 1, bytes.size(), dst);
                        std::fclose(dst);
                    }
                }
            }
            std::fclose(src);
        }
    }
    refreshLevelnameFile();
    utils::MainLogger::get().debug("[LevelDB] created fresh world metadata (level.dat, level.dat_old, levelname.txt)");
}

void LevelDB::setSeed(int64_t s) {
    if (seed_ == s)
        return;
    seed_ = s;
    onMetadataChanged();
}

void LevelDB::setCurrentTime(int64_t t) {
    if (time_ == t)
        return;
    time_ = t;
    onMetadataChanged();
}

void LevelDB::setSpawn(const math::Vector3& v) {
    if (spawn_.x == v.x && spawn_.y == v.y && spawn_.z == v.z)
        return;
    spawn_ = v;
    onMetadataChanged();
}

void LevelDB::onMetadataChanged() {
    if (!meta_root_)
        return;
    applyMetadata();
    if (fresh_world_) {
        // Creation time: the world must be born complete — the seed and
        // spawn the server just settled go into level.dat now, not at the
        // first autosave.
        if (!LevelDBChunkCodec::write_level_dat(path_, meta_root_))
            utils::MainLogger::get().warn("[LevelDB] failed to write fresh level.dat");
    }
    // Existing worlds keep the previous save untouched until the next
    // saveLevelData() (autosave / /save / shutdown).
}

void LevelDB::applyMetadata() {
    if (!meta_root_)
        return;
    using namespace cursestone::nbt;
    set_long(meta_root_, "Time", time_);
    set_int(meta_root_, "DayTime", (int32_t)time_);
    set_long(meta_root_, "RandomSeed", seed_);
    set_int(meta_root_, "SpawnX", (int32_t)spawn_.x);
    set_int(meta_root_, "SpawnY", (int32_t)spawn_.y);
    set_int(meta_root_, "SpawnZ", (int32_t)spawn_.z);
    set_long(meta_root_, "LastPlayed",
             (int64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
                 std::chrono::system_clock::now().time_since_epoch())
                 .count());
    set_string(meta_root_, "LevelName", level_name_.c_str());
    set_string(meta_root_, "generatorName", generator_name_.c_str());
    // Fields CurseStone does not manage keep whatever the device/creator
    // wrote; only fill the well-known ones when they are missing so a
    // hand-made or device level.dat never loses its own values.
    if (get(meta_root_, "GameType") == nullptr)
        set_int(meta_root_, "GameType", 0);
    if (get(meta_root_, "hardcore") == nullptr)
        set_byte(meta_root_, "hardcore", 0);
    if (get(meta_root_, "initialized") == nullptr)
        set_byte(meta_root_, "initialized", 1);
    if (get(meta_root_, "version") == nullptr)
        set_int(meta_root_, "version", 19133);
    if (get(meta_root_, "generatorVersion") == nullptr)
        set_int(meta_root_, "generatorVersion", 1);
    if (get(meta_root_, "SizeOnDisk") == nullptr)
        set_long(meta_root_, "SizeOnDisk", 0);
    if (get(meta_root_, "raining") == nullptr)
        set_byte(meta_root_, "raining", 0);
    if (get(meta_root_, "rainTime") == nullptr)
        set_int(meta_root_, "rainTime", 0);
    if (get(meta_root_, "thundering") == nullptr)
        set_byte(meta_root_, "thundering", 0);
    if (get(meta_root_, "thunderTime") == nullptr)
        set_int(meta_root_, "thunderTime", 0);
    if (get(meta_root_, "generatorOptions") == nullptr)
        set_string(meta_root_, "generatorOptions", "");
    if (get(meta_root_, "GameRules") == nullptr) {
        Tag* rules = new CompoundTag("GameRules");
        set(meta_root_, "GameRules", rules);
    }
}

void LevelDB::refreshLevelnameFile() const {
    std::string p = path_ + "/levelname.txt";
    std::FILE* fp = std::fopen(p.c_str(), "rb");
    if (fp) {
        char buf[256];
        size_t n = std::fread(buf, 1, sizeof(buf) - 1, fp);
        std::fclose(fp);
        buf[n] = '\0';
        std::string existing(buf);
        while (!existing.empty() &&
               (existing.back() == '\n' || existing.back() == '\r'))
            existing.pop_back();
        if (existing == level_name_)
            return; // unchanged: no pointless disk write
    }
    fp = std::fopen(p.c_str(), "wb");
    if (!fp)
        return;
    std::fwrite(level_name_.data(), 1, level_name_.size(), fp);
    std::fclose(fp);
}

void LevelDB::saveLevelData() {
    if (!meta_root_)
        return;
    applyMetadata();
    if (!LevelDBChunkCodec::write_level_dat(path_, meta_root_)) {
        utils::MainLogger::get().warn("[LevelDB] failed to write level.dat");
        return;
    }
    refreshLevelnameFile();
}

// ============================================================================
// Chunk I/O
// ============================================================================

std::optional<std::shared_ptr<FullChunk>> LevelDB::loadChunk(int32_t x, int32_t z) {
    if (!engine_.is_open())
        return std::nullopt;

    // Terrain record first: a chunk without one was never saved.
    std::optional<std::string> terrain = engine_.get(
        key_str(LevelDBChunkCodec::record_key(x, z, LevelDBChunkCodec::kRecordTerrain)));
    if (!terrain || terrain->size() < LevelDBChunkCodec::kLegacyTerrainSize)
        return std::nullopt;

    mcp_chunk_t* chunk = LevelDBChunkCodec::decode_legacy_terrain(
        (const uint8_t*)terrain->data(), terrain->size(), x, z);
    if (!chunk) {
        utils::MainLogger::get().warn("[LevelDB] corrupt terrain record for chunk " +
                                      std::to_string(x) + "," + std::to_string(z));
        return std::nullopt;
    }

    // Flags (default 0x03 generated+populated, like every reader of the
    // format assumes when the record is absent).
    uint8_t flags = 0x03;
    if (auto f = engine_.get(
            key_str(LevelDBChunkCodec::record_key(x, z, LevelDBChunkCodec::kRecordFlags)))) {
        if (!f->empty())
            flags = (uint8_t)(*f)[0];
    }
    chunk->terrain_populated = (flags & 0x03) != 0;
    chunk->light_populated = (flags & 0x04) != 0;

    // Tile entities + entities: streams of little-endian compounds; each
    // blob lands in the chunk's blob channel exactly as the Anvil path
    // produces them (World hydrates tiles/mobs from there).
    for (uint8_t record : {LevelDBChunkCodec::kRecordTiles,
                           LevelDBChunkCodec::kRecordEntities}) {
        auto blob = engine_.get(key_str(LevelDBChunkCodec::record_key(x, z, record)));
        if (!blob || blob->empty())
            continue;
        std::vector<std::vector<uint8_t>> parts;
        if (!LevelDBChunkCodec::split_compound_stream((const uint8_t*)blob->data(),
                                                      blob->size(), &parts))
            continue;
        for (const auto& part : parts) {
            if (record == LevelDBChunkCodec::kRecordTiles)
                mcp_chunk_add_tile_entity(chunk, part.data(), part.size());
            else
                mcp_chunk_add_entity(chunk, part.data(), part.size());
        }
    }

    return std::make_shared<ChunkAdapter>(chunk);
}

void LevelDB::saveChunk(const std::shared_ptr<FullChunk>& chunk) {
    if (!chunk || !engine_.is_open())
        return;
    auto* adapter = dynamic_cast<ChunkAdapter*>(chunk.get());
    if (!adapter || !adapter->chunk())
        return;
    const mcp_chunk_t* raw = adapter->chunk();

    // MAIN-thread part: copy the arrays the worker will read (snapshot of
    // the block/light data, byte copies of the tile/entity blobs). The
    // transpose, batching and any table flush happen on the worker.
    performance::SaveProfiler::Scope snap_probe(
        performance::SaveStage::Snapshot);
    SaveJob job;
    job.x = raw->x;
    job.z = raw->z;
    job.snapshot = mcp_chunk_wire_snapshot_create(raw);
    if (!job.snapshot)
        return;
    for (size_t i = 0; i < raw->tile_entity_count; ++i) {
        const uint8_t* blob = raw->tile_entities[i];
        job.tiles.insert(job.tiles.end(), blob, blob + raw->tile_entity_lens[i]);
    }
    for (size_t i = 0; i < raw->entity_count; ++i) {
        const uint8_t* blob = raw->entities[i];
        job.entities.insert(job.entities.end(), blob, blob + raw->entity_lens[i]);
    }
    job.flags = (uint8_t)(((raw->terrain_populated ? 0x03 : 0x00)) |
                           (raw->light_populated ? 0x04 : 0x00));
    performance::SaveProfiler::noteChunkSubmitted();
    enqueueSave(std::move(job));
}

void LevelDB::unloadChunk(int32_t x, int32_t z) {
    (void)x;
    (void)z; // ChunkManager owns residency; the provider only does disk I/O.
}

std::vector<std::pair<int32_t, int32_t>> LevelDB::listChunks() const {
    std::vector<std::pair<int32_t, int32_t>> out;
    if (!engine_.is_open())
        return out;
    // Every key is 9 bytes: LE x, LE z, record byte. Terrain records (0x30)
    // mark a saved chunk.
    engine_.scan("", [&](const std::string& key, const std::string&) {
        if (key.size() != 9)
            return;
        if ((uint8_t)key[8] != LevelDBChunkCodec::kRecordTerrain)
            return;
        int32_t cx = (int32_t)((uint32_t)(uint8_t)key[0] |
                               ((uint32_t)(uint8_t)key[1] << 8) |
                               ((uint32_t)(uint8_t)key[2] << 16) |
                               ((uint32_t)(uint8_t)key[3] << 24));
        int32_t cz = (int32_t)((uint32_t)(uint8_t)key[4] |
                               ((uint32_t)(uint8_t)key[5] << 8) |
                               ((uint32_t)(uint8_t)key[6] << 16) |
                               ((uint32_t)(uint8_t)key[7] << 24));
        out.emplace_back(cx, cz);
    });
    return out;
}

// ============================================================================
// Async writer — mirrors the Anvil pipeline (main thread snapshots, worker
// transposes + batches + the engine flushes/compacts on its own thresholds).
// ============================================================================
void LevelDB::startWorker() {
    {
        std::lock_guard<std::mutex> lk(job_mtx_);
        worker_stop_ = false;
    }
    worker_ = std::thread([this] {
        mcp_set_thread_name("CS-Leveldb");
        workerLoop();
    });
}

void LevelDB::stopWorker() {
    {
        std::lock_guard<std::mutex> lk(job_mtx_);
        worker_stop_ = true;
    }
    job_cv_.notify_all();
    if (worker_.joinable())
        worker_.join(); // exits only after the queue is drained
}

void LevelDB::enqueueSave(SaveJob&& job) {
    // Backpressure point: when the worker has fallen behind (fast
    // exploration marks chunks dirty faster than the single writer
    // drains them), this wait is where the caller stalls. It is timed
    // as EnqueueBlocked — a non-zero count is the queue's verdict on
    // whether the drain budget matches the production rate.
    const bool probe = performance::SaveProfiler::enabled();
    const auto wait_t0 = probe ? std::chrono::steady_clock::now()
                               : std::chrono::steady_clock::time_point{};
    size_t depth = 0, bytes = 0;
    bool queue_was_full = false;
    {
        std::unique_lock<std::mutex> lk(job_mtx_);
        // Non-blocking fast-path check BEFORE waiting: only callers that
        // actually found a full queue count as backpressure events.
        queue_was_full = job_queue_.size() >= kMaxQueuedSaves;
        job_cv_.wait(lk, [&] {
            return job_queue_.size() < kMaxQueuedSaves;
        });
        queued_bytes_ += job_bytes(job);
        job_queue_.push_back(std::move(job));
        depth = job_queue_.size();
        bytes = queued_bytes_;
    }
    if (probe) {
        performance::SaveProfiler::add(
            performance::SaveStage::EnqueueBlocked,
            (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - wait_t0)
                .count());
        performance::SaveProfiler::add(performance::SaveStage::Enqueue, 0);
        if (queue_was_full)
            performance::SaveProfiler::noteEnqueueBlocked();
        // Gauge note after the push: depth+bytes as the queue now stands.
        performance::SaveProfiler::noteQueueDepth(depth, bytes);
    }
    job_cv_.notify_one();
}

void LevelDB::workerLoop() {
    for (;;) {
        SaveJob job;
        {
            std::unique_lock<std::mutex> lk(job_mtx_);
            job_cv_.wait(lk, [&] {
                return worker_stop_ || !job_queue_.empty();
            });
            if (job_queue_.empty()) {
                if (worker_stop_)
                    return;
                continue;
            }
            job = std::move(job_queue_.front());
            job_queue_.pop_front();
            job_busy_ = true;
            queued_bytes_ -= job_bytes(job);
            // DEADLOCK FIX (the reported "server freezes during autosave
            // with ~2500 chunks"): a producer blocked in enqueueSave()
            // waits on job_cv_ for QUEUE SPACE, and only a pop creates
            // space. Without this notify, the first time the queue hit
            // kMaxQueuedSaves the producer slept forever — flushDirty()
            // held the chunk-store mutex behind it and the whole server
            // (ticks, console dispatch, generation) froze with it; Ctrl+C
            // only flipped a flag the tick loop could never reach again.
            // Wake every waiting producer on every pop.
            lk.unlock();
            job_cv_.notify_all();
            lk.lock();
            // Live gauge note from the consumer side (depth as it stands
            // after the pop; the enqueue side notes after each push).
            if (performance::SaveProfiler::enabled())
                performance::SaveProfiler::noteQueueDepth(job_queue_.size(),
                                                          queued_bytes_);
        }

        // Transpose the snapshot into the legacy terrain record (worker
        // thread — touches only the snapshot it owns). The whole job
        // (transpose + batch write + any table flush the engine decides
        // to run inline) is timed as WorkerPut.
        performance::SaveProfiler::Scope put_probe(performance::SaveStage::WorkerPut);
        size_t terrain_len = 0;
        uint8_t* terrain =
            mcp_chunk_snapshot_legacy_terrain_serialize(job.snapshot, &terrain_len);

        if (terrain && terrain_len >= LevelDBChunkCodec::kLegacyTerrainSize &&
            engine_.is_open()) {
            // One batch per chunk: terrain + flags + version + tiles +
            // entities share one sequence number and one WAL record.
            std::vector<std::pair<std::string, std::optional<std::string>>> ops;
            ops.reserve(5);
            auto push_op = [&](uint8_t record, std::optional<std::string> value) {
                ops.emplace_back(
                    key_str(LevelDBChunkCodec::record_key(job.x, job.z, record)),
                    std::move(value));
            };
            push_op(LevelDBChunkCodec::kRecordTerrain,
                    std::string((const char*)terrain, terrain_len));
            push_op(LevelDBChunkCodec::kRecordFlags, std::string(1, (char)job.flags));
            push_op(LevelDBChunkCodec::kRecordVersion,
                    std::string(1, (char)LevelDBChunkCodec::kLegacyChunkVersion));
            if (!job.tiles.empty())
                push_op(LevelDBChunkCodec::kRecordTiles, std::string(
                    (const char*)job.tiles.data(), job.tiles.size()));
            if (!job.entities.empty())
                push_op(LevelDBChunkCodec::kRecordEntities, std::string(
                    (const char*)job.entities.data(), job.entities.size()));
            const bool put_ok = engine_.put_batch(ops);
            performance::SaveProfiler::noteChunkWritten(put_ok);
            if (!put_ok) {
                utils::MainLogger::get().warn("[LevelDB] batch write failed for chunk " +
                                              std::to_string(job.x) + "," +
                                              std::to_string(job.z));
            }
        } else if (!terrain) {
            utils::MainLogger::get().warn("[LevelDB] failed to transpose snapshot for chunk " +
                                          std::to_string(job.x) + "," +
                                          std::to_string(job.z));
        }

        // Single free site: the transpose result and the snapshot.
        if (terrain)
            free(terrain);
        mcp_chunk_wire_snapshot_free(job.snapshot);

        {
            std::lock_guard<std::mutex> lk(job_mtx_);
            job_busy_ = false;
            if (job_queue_.empty())
                done_cv_.notify_all();
        }
    }
}

void LevelDB::flushPendingSaves() {
    std::unique_lock<std::mutex> lk(job_mtx_);
    if (!worker_.joinable()) {
        lk.unlock();
        engine_.flush();
        return; // worker never started (no saves accepted)
    }
    // The caller (main thread on /save, shutdown, world unload) waits
    // here for EVERY queued chunk plus the batch in flight. Timed as
    // DrainWait: this number IS the main-thread stall of a full save,
    // before any final engine flush is added on top.
    performance::SaveProfiler::Scope drain_probe(performance::SaveStage::DrainWait);
    done_cv_.wait(lk, [&] {
        return job_queue_.empty() && !job_busy_;
    });
    lk.unlock();
    performance::SaveProfiler::Scope flush_probe(performance::SaveStage::EngineFlushCall);
    engine_.flush(); // force the memtable to a table: /save is durable now
}

} // namespace cursestone::world::format::leveldb
