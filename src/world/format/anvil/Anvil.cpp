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
// Anvil.cpp — Nukkit Anvil WorldProvider ported to CurseStone C++.
//
// Wires together AnvilRegionLoader (the .mca file codec) and
// AnvilChunkCodec (the per-chunk NBT codec) behind CurseStone's
// WorldProvider interface. level.dat is GZIP-compressed big-endian NBT
// matching Nukkit's Anvil.generate() output.
#include "Anvil.h"
#include "AnvilChunkCodec.h"
#include "../WorldProviderManager.h"
#include "../ChunkAdapter.h"
#include "../Chunk.h"
#include "../../../nbt/NBT.h"
#include "../../../utils/CRaii.h"
#include "../../../nbt/tag/CompoundTag.h"
#include "../../../nbt/tag/ByteArrayTag.h"
#include "../../../nbt/tag/IntArrayTag.h"
#include "../../../nbt/tag/IntTag.h"
#include "../../../nbt/tag/ByteTag.h"
#include "../../../nbt/tag/LongTag.h"
#include "../../../nbt/tag/StringTag.h"
#include "../../../nbt/tag/Tag.h"
#include "../../../utils/MainLogger.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include "../../../utils/PlatformCompat.h"
#include <zlib.h>

namespace cursestone::world::format::anvil {

// ============================================================================
// Registrar — installs "anvil" into WorldProviderManager at process start.
// Also registers the friendly alias "mcanvil" and "mca" for legacy configs.
// ============================================================================
AnvilRegistrar g_anvil_registrar_;
AnvilRegistrar::AnvilRegistrar() {
    auto& mgr = WorldProviderManager::getInstance();
    mgr.registerProvider("anvil", []() {
        return std::unique_ptr<WorldProvider>(new Anvil());
    });
    mgr.registerProvider("mcanvil", []() {
        return std::unique_ptr<WorldProvider>(new Anvil());
    });
    mgr.registerProvider("mca", []() {
        return std::unique_ptr<WorldProvider>(new Anvil());
    });
}

// ============================================================================
// Filesystem helpers
// ============================================================================
static bool ensure_dir(const std::string& path) {
    struct stat st{};
    if (::stat(path.c_str(), &st) == 0)
        return S_ISDIR(st.st_mode);
    if (errno != ENOENT)
        return false;
    return ::mkdir(path.c_str(), 0775) == 0;
}

static int64_t current_millis() {
    return (int64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

static int64_t current_unix_seconds() {
    return (int64_t)std::time(nullptr);
}

// Read entire file into a buffer. Returns false on I/O error.
static bool read_file(const std::string& path, std::vector<uint8_t>* out) {
    FILE* fp = std::fopen(path.c_str(), "rb");
    if (!fp)
        return false;
    std::fseek(fp, 0, SEEK_END);
    long sz = std::ftell(fp);
    std::fseek(fp, 0, SEEK_SET);
    if (sz <= 0) {
        std::fclose(fp);
        return false;
    }
    out->resize((size_t)sz);
    bool ok = (std::fread(out->data(), 1, (size_t)sz, fp) == (size_t)sz);
    std::fclose(fp);
    return ok;
}

static bool write_file(const std::string& path, const uint8_t* data, size_t len) {
    FILE* fp = std::fopen(path.c_str(), "wb");
    if (!fp)
        return false;
    bool ok = (std::fwrite(data, 1, len, fp) == len);
    std::fclose(fp);
    return ok;
}

// GZIP-compress `src` → `out`. Returns false on deflate error.
static bool gzip_compress(const uint8_t* src, size_t src_len, std::vector<uint8_t>* out) {
    z_stream zs{};
    if (deflateInit2(&zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED,
                     15 | 16, // 15 = max window bits, +16 = gzip header
                     8, Z_DEFAULT_STRATEGY) != Z_OK) {
        return false;
    }
    size_t bound = deflateBound(&zs, (uLong)src_len);
    out->resize(bound);
    zs.next_in = (Bytef*)src;
    zs.avail_in = (uInt)src_len;
    zs.next_out = (Bytef*)out->data();
    zs.avail_out = (uInt)out->size();
    int rc = deflate(&zs, Z_FINISH);
    deflateEnd(&zs);
    if (rc != Z_STREAM_END)
        return false;
    out->resize(bound - zs.avail_out);
    return true;
}

// GZIP-decompress `src` → `out`. Returns false on inflate error.
static bool gzip_decompress(const uint8_t* src, size_t src_len, std::vector<uint8_t>* out) {
    // Ceiling per record: chunk payloads are at most ~1 MiB compressed and
    // a few MB inflated; a corrupt/hostile region file must not be able to
    // ask for gigabytes.
    static const size_t kMaxDecompressed = 64u << 20; // 64 MiB
    z_stream zs{};
    if (inflateInit2(&zs, 15 | 16) != Z_OK)
        return false; // gzip
    zs.next_in = (Bytef*)src;
    zs.avail_in = (uInt)src_len;

    // Grow output buffer incrementally.
    out->resize(0);
    out->reserve(src_len * 4);
    uint8_t chunk[65536];
    int rc = Z_OK;
    do {
        zs.next_out = chunk;
        zs.avail_out = sizeof(chunk);
        rc = inflate(&zs, Z_NO_FLUSH);
        if (rc != Z_OK && rc != Z_STREAM_END) {
            inflateEnd(&zs);
            return false;
        }
        size_t produced = sizeof(chunk) - zs.avail_out;
        if (produced > 0) {
            if (out->size() + produced > kMaxDecompressed) {
                inflateEnd(&zs);
                return false;
            }
            out->insert(out->end(), chunk, chunk + produced);
        }
    } while (rc != Z_STREAM_END && zs.avail_in > 0);
    inflateEnd(&zs);
    return rc == Z_STREAM_END;
}

// Nivel de zlib al escribir un chunk en la region. El nivel NO forma
// parte del formato: el byte de compresion del .mca es 2 (ZLIB) y
// cualquier descompresor zlib lee el stream sea cual sea el nivel con
// el que se genero.
//
// Por que no el default (6): medido sobre el codigo de abajo con chunks
// de terreno normal (52.5 KB de NBT -> 8.5 KB comprimidos), el nivel 6
// se come el ~90% de lo que tarda un autosave:
//
//   nivel 6 : 1.27 ms/chunk -> 8.5 KB      (349 chunks = 445 ms)
//   nivel 1 : 0.45 ms/chunk -> 9.7 KB      (~3x mas rapido, +14% de espacio)
//   nivel 9 : 8.90 ms/chunk -> 8.4 KB      (lo contrario, absurdo)
//
// El autosave escribe en el hilo principal, asi que esos ms son freeze
// de tick. El nivel 1 apenas compra espacio (14%) a cambio de 3x menos
// CPU, que es exactamente el intercambio que quiere un servidor en vivo.
static constexpr int kChunkCompressionLevel = 1;

// ZLIB-compress (raw deflate) — used for chunk payloads in .mca files.
static bool zlib_compress(const uint8_t* src, size_t src_len, std::vector<uint8_t>* out) {
    uLongf bound = compressBound((uLong)src_len);
    out->resize(bound);
    uLongf dest_len = bound;
    int rc = compress2(out->data(), &dest_len, src, (uLong)src_len, kChunkCompressionLevel);
    if (rc != Z_OK) {
        out->clear();
        return false;
    }
    out->resize(dest_len);
    return true;
}

// ZLIB-decompress (raw inflate).
static bool zlib_decompress(const uint8_t* src, size_t src_len, std::vector<uint8_t>* out) {
    static const size_t kMaxDecompressed = 64u << 20; // 64 MiB (see gzip_decompress)
    z_stream zs{};
    if (inflateInit(&zs) != Z_OK)
        return false;
    zs.next_in = (Bytef*)src;
    zs.avail_in = (uInt)src_len;
    out->resize(0);
    out->reserve(src_len * 4);
    uint8_t chunk[65536];
    int rc = Z_OK;
    do {
        zs.next_out = chunk;
        zs.avail_out = sizeof(chunk);
        rc = inflate(&zs, Z_NO_FLUSH);
        if (rc != Z_OK && rc != Z_STREAM_END) {
            inflateEnd(&zs);
            return false;
        }
        size_t produced = sizeof(chunk) - zs.avail_out;
        if (produced > 0) {
            if (out->size() + produced > kMaxDecompressed) {
                inflateEnd(&zs);
                return false;
            }
            out->insert(out->end(), chunk, chunk + produced);
        }
    } while (rc != Z_STREAM_END && zs.avail_in > 0);
    inflateEnd(&zs);
    return rc == Z_STREAM_END;
}

// ============================================================================
// Anvil ctor / dtor
// ============================================================================
Anvil::Anvil() = default;
Anvil::~Anvil() {
    shutdown();
}

// ============================================================================
// init — open the world directory, read level.dat, create region/ if missing.
// ============================================================================
void Anvil::init(const std::string& path) {
    path_ = path;
    ensure_dir(path_);
    ensure_dir(path_ + "/region");

    // If level.dat is missing, write a fresh one with defaults (mirrors
    // Nukkit's Anvil.generate()).
    std::string level_dat = path_ + "/level.dat";
    FILE* fp = std::fopen(level_dat.c_str(), "rb");
    if (!fp) {
        // No level.dat → generate defaults. Seed/spawn come from
        // WorldConfig via setSpawn/setCurrentTime (called by World ctor).
        // We'll write level.dat on shutdown/saveMetadata().
        utils::MainLogger::get().info("[Anvil] No level.dat found at " + level_dat +
                                      "; will create on save.");
    } else {
        std::fclose(fp);
        loadMetadata();
    }
    utils::MainLogger::get().info("[Anvil] Initialized at " + path_);

    // Background chunk writer — the reference-style autosave pipeline.
    // Main thread only ever encodes; compression + region I/O live here.
    startWorker();
}

void Anvil::shutdown() {
    // Stop the worker AFTER it has written every queued chunk (stopWorker
    // drains the queue before joining), so a graceful shutdown never
    // loses accepted writes. Region handles are closed only once no
    // other thread can be inside them.
    stopWorker();
    {
        std::lock_guard<std::mutex> io(io_mtx_);
        for (auto& [_, rf_ptr] : regions_) {
            if (rf_ptr)
                rf_ptr->close();
        }
        regions_.clear();
        region_lru_.clear();
    }
    saveMetadata();
}

// ============================================================================
// Region management — ALL region-map access is serialized by io_mtx_:
// the worker thread writes chunks while the main thread loads/evicts.
// ============================================================================
std::string Anvil::regionPath(int32_t cx, int32_t cz) const {
    int32_t rx = (cx >= 0) ? (cx / 32) : ((cx - 31) / 32);
    int32_t rz = (cz >= 0) ? (cz / 32) : ((cz - 31) / 32);
    char buf[128];
    std::snprintf(buf, sizeof(buf), "%s/region/r.%d.%d.mca", path_.c_str(), (int)rx, (int)rz);
    return std::string(buf);
}

AnvilRegionLoader* Anvil::regionFor(int32_t cx, int32_t cz, bool create) {
    // Caller must already hold io_mtx_ (regionFor mutates the LRU list and
    // may open/evict region files; the returned loader is only valid while
    // the lock is held).
    std::string p = regionPath(cx, cz);
    auto it = regions_.find(p);
    if (it != regions_.end()) {
        // Bump to MRU.
        auto lru_it = std::find(region_lru_.begin(), region_lru_.end(), p);
        if (lru_it != region_lru_.end()) {
            region_lru_.erase(lru_it);
            region_lru_.push_back(p);
        }
        return it->second.get();
    }
    if (regions_.size() >= kMaxOpenRegions)
        evictOldestRegion();
    int32_t rx = (cx >= 0) ? (cx / 32) : ((cx - 31) / 32);
    int32_t rz = (cz >= 0) ? (cz / 32) : ((cz - 31) / 32);
    auto rf = std::make_unique<AnvilRegionLoader>();
    if (!rf->open(p, rx, rz))
        return nullptr;
    AnvilRegionLoader* raw = rf.get();
    regions_[p] = std::move(rf);
    region_lru_.push_back(p);
    return raw;
}

void Anvil::evictOldestRegion() {
    if (region_lru_.empty())
        return;
    std::string oldest = region_lru_.front();
    region_lru_.erase(region_lru_.begin());
    auto it = regions_.find(oldest);
    if (it != regions_.end()) {
        if (it->second)
            it->second->close();
        regions_.erase(it);
    }
}

// ============================================================================
// Chunk I/O
// ============================================================================
std::optional<std::shared_ptr<FullChunk>> Anvil::loadChunk(int32_t x, int32_t z) {
    // The loader pointer must stay valid until readChunk() is done — keep
    // the whole region lookup + read under io_mtx_ so a concurrent worker
    // write cannot evict/reopen the region between the two calls.
    std::vector<uint8_t> payload;
    {
        std::lock_guard<std::mutex> io(io_mtx_);
        AnvilRegionLoader* rf = regionFor(x, z, /*create=*/false);
        if (!rf)
            return std::nullopt;
        auto read = rf->readChunk(x & 31, z & 31);
        if (!read)
            return std::nullopt;
        payload = std::move(*read);
    }

    // Inflate ZLIB.
    std::vector<uint8_t> inflated;
    if (!zlib_decompress(payload.data(), payload.size(), &inflated)) {
        utils::MainLogger::get().warn("[Anvil] Failed to inflate chunk " + std::to_string(x) + "," +
                                      std::to_string(z));
        return std::nullopt;
    }

    mcp_chunk_t* raw = anvil_chunk_decode(inflated.data(), inflated.size());
    if (!raw)
        return std::nullopt;

    auto adapter = std::make_shared<ChunkAdapter>(raw);
    return adapter;
}

// ============================================================================
// saveChunk — ASYNC (reference-style autosave pipeline).
//
// Split by thread:
//   * MAIN thread (here): anvil_chunk_encode() walks the live
//     mcp_chunk_t — that read is only safe while the world is quiesced
//     on the calling thread, so it stays up front. Cost measured at
//     ~0.1 ms/chunk (the 53 KB NBT is essentially memcpy-bound).
//   * WORKER thread (workerLoop): ZLIB compression (0.45 ms/chunk at
//     level 1) and the region-file write. The old synchronous path spent
//     those milliseconds — plus any disk stall, which is where the
//     reported 12 ms ticks came from — inside the server tick.
//
// FIFO ordering: a chunk re-dirtied while its snapshot is still queued
// gets a second job enqueued later; one worker + FIFO guarantees the
// newer snapshot is written after the older one.
//
// The dirty flag is NOT touched here — ChunkManager::flushDirty/
// flushDirtyBudget/unloadChunk clear it right after this call, which
// now means "a snapshot has been accepted by the writer", exactly the
// guarantee the async pipeline can make.
// ============================================================================
void Anvil::saveChunk(const std::shared_ptr<FullChunk>& chunk) {
    if (!chunk)
        return;
    auto* adapter = dynamic_cast<ChunkAdapter*>(chunk.get());
    if (!adapter)
        return;
    mcp_chunk_t* raw = adapter->chunk();
    if (!raw)
        return;

    raw->last_update = current_millis();

    // Encode NBT big-endian (main thread — reads the live chunk).
    size_t nbt_len = 0;
    utils::BufferPtr nbt_buf(anvil_chunk_encode(raw, &nbt_len));
    if (!nbt_buf || nbt_len == 0) {
        utils::MainLogger::get().warn("[Anvil] Failed to encode chunk " + std::to_string(raw->x) +
                                      "," + std::to_string(raw->z));
        return;
    }

    SaveJob job;
    job.x = raw->x;
    job.z = raw->z;
    job.nbt.assign(nbt_buf.get(), nbt_buf.get() + nbt_len);
    enqueueSave(std::move(job));
}

// ============================================================================
// Async writer plumbing
// ============================================================================
void Anvil::startWorker() {
    {
        std::lock_guard<std::mutex> lk(job_mtx_);
        worker_stop_ = false;
    }
    worker_ = std::thread([this] {
        workerLoop();
    });
}

void Anvil::stopWorker() {
    {
        std::lock_guard<std::mutex> lk(job_mtx_);
        worker_stop_ = true;
    }
    job_cv_.notify_all();
    // join() waits until the worker has written EVERY queued chunk: it
    // only exits when worker_stop_ is set AND the queue is drained.
    if (worker_.joinable())
        worker_.join();
}

void Anvil::enqueueSave(SaveJob&& job) {
    std::unique_lock<std::mutex> lk(job_mtx_);
    // Bounded backpressure: if the disk is slower than the drain budget
    // for a long stretch, block the (already budgeted) drainer instead of
    // ballooning memory. The worker keeps draining even past worker_stop_,
    // so this cannot deadlock against shutdown.
    job_cv_.wait(lk, [&] {
        return job_queue_.size() < kMaxQueuedSaves;
    });
    job_queue_.push_back(std::move(job));
    lk.unlock();
    job_cv_.notify_one();
}

void Anvil::workerLoop() {
    for (;;) {
        SaveJob job;
        {
            std::unique_lock<std::mutex> lk(job_mtx_);
            job_cv_.wait(lk, [&] {
                return worker_stop_ || !job_queue_.empty();
            });
            if (job_queue_.empty()) {
                if (worker_stop_) {
                    done_cv_.notify_all();
                    return;
                }
                continue;
            }
            job = std::move(job_queue_.front());
            job_queue_.pop_front();
            job_busy_ = true;
        }

        // ZLIB OUTSIDE io_mtx_ — pure CPU over job-local memory.
        std::vector<uint8_t> compressed;
        if (zlib_compress(job.nbt.data(), job.nbt.size(), &compressed)) {
            std::lock_guard<std::mutex> io(io_mtx_);
            AnvilRegionLoader* rf = regionFor(job.x, job.z, /*create=*/true);
            if (!rf) {
                utils::MainLogger::get().warn("[Anvil] Failed to open region for chunk " +
                                              std::to_string(job.x) + "," + std::to_string(job.z));
            } else if (!rf->writeChunk(job.x & 31, job.z & 31, kCompressionZlib, compressed)) {
                utils::MainLogger::get().warn("[Anvil] Failed to write chunk " +
                                              std::to_string(job.x) + "," + std::to_string(job.z));
            }
        } else {
            utils::MainLogger::get().warn("[Anvil] zlib compress failed for chunk " +
                                          std::to_string(job.x) + "," + std::to_string(job.z));
        }
        job.nbt.clear();
        job.nbt.shrink_to_fit();

        {
            std::lock_guard<std::mutex> lk(job_mtx_);
            job_busy_ = false;
            if (job_queue_.empty())
                done_cv_.notify_all();
        }
    }
}

void Anvil::flushPendingSaves() {
    std::unique_lock<std::mutex> lk(job_mtx_);
    if (!worker_.joinable())
        return; // worker never started
    done_cv_.wait(lk, [&] {
        return job_queue_.empty() && !job_busy_;
    });
}

void Anvil::unloadChunk(int32_t /*x*/, int32_t /*z*/) {
    // Chunks themselves are managed by ChunkManager; the provider just
    // handles disk I/O. Nothing to do here.
}

// ============================================================================
// listChunks — enumerate every chunk coordinate present on disk.
// ============================================================================
std::vector<std::pair<int32_t, int32_t>> Anvil::listChunks() const {
    std::vector<std::pair<int32_t, int32_t>> out;
    std::string regions_dir = path_ + "/region";
    DIR* dir = ::opendir(regions_dir.c_str());
    if (!dir)
        return out;
    struct dirent* ent;
    while ((ent = ::readdir(dir)) != nullptr) {
        std::string name = ent->d_name;
        if (name.size() < 8)
            continue;
        if (name.substr(0, 2) != "r.")
            continue;
        if (name.substr(name.size() - 4) != ".mca")
            continue;
        size_t first_dot = name.find('.', 2);
        if (first_dot == std::string::npos)
            continue;
        size_t second_dot = name.find('.', first_dot + 1);
        if (second_dot == std::string::npos)
            continue;
        std::string rx_str = name.substr(2, first_dot - 2);
        std::string rz_str = name.substr(first_dot + 1, second_dot - first_dot - 1);
        int32_t rx = 0, rz = 0;
        try {
            rx = std::stoi(rx_str);
            rz = std::stoi(rz_str);
        } catch (...) {
            continue;
        }
        // Open the region file and walk its location table to enumerate
        // the actual chunk coordinates that exist on disk.
        auto* self = const_cast<Anvil*>(this);
        AnvilRegionLoader* rf = self->regionFor(rx * 32, rz * 32, /*create=*/false);
        if (!rf)
            continue;
        for (int32_t lz = 0; lz < 32; ++lz) {
            for (int32_t lx = 0; lx < 32; ++lx) {
                if (rf->hasChunk(lx, lz)) {
                    out.emplace_back(rx * 32 + lx, rz * 32 + lz);
                }
            }
        }
    }
    ::closedir(dir);
    return out;
}

// ============================================================================
// level.dat — Nukkit-style "Data" compound, GZIP, big-endian.
// ============================================================================
void Anvil::loadMetadata() {
    std::string p = path_ + "/level.dat";
    std::vector<uint8_t> raw;
    if (!read_file(p, &raw)) {
        seed_ = 0;
        time_ = 0;
        spawn_ = math::Vector3{0.0, 64.0, 0.0};
        return;
    }

    // GZIP-inflate.
    std::vector<uint8_t> inflated;
    if (!gzip_decompress(raw.data(), raw.size(), &inflated)) {
        utils::MainLogger::get().warn("[Anvil] level.dat gzip inflate failed");
        return;
    }

    utils::TagGuard root(mcp_nbt_read_bytes(inflated.data(), inflated.size(),
                                            /*little_endian=*/false));
    if (!root || mcp_tag_get_type(root.get()) != MCP_TAG_COMPOUND)
        return;
    const mcp_tag_t* data = mcp_compound_tag_get_compound(root.get(), "Data");
    if (!data)
        return;

    seed_ = mcp_compound_tag_get_long(data, "RandomSeed", 0);
    time_ = mcp_compound_tag_get_long(data, "Time", 0);
    int64_t day = mcp_compound_tag_get_long(data, "DayTime", 0);
    if (time_ == 0 && day != 0)
        time_ = day;
    int32_t sx = mcp_compound_tag_get_int(data, "SpawnX", 0);
    int32_t sy = mcp_compound_tag_get_int(data, "SpawnY", 64);
    int32_t sz = mcp_compound_tag_get_int(data, "SpawnZ", 0);
    spawn_ = math::Vector3{(double)sx, (double)sy, (double)sz};
    const char* gen = mcp_compound_tag_get_string(data, "generatorName", "normal");
    if (gen)
        generator_name_ = gen;
}

void Anvil::saveMetadata() {
    // Build root compound "".
    utils::TagGuard root(mcp_compound_tag_new_named(""));
    if (!root)
        return;

    // Build "Data" compound (mirrors Nukkit's Anvil.generate()).
    utils::TagGuard data(mcp_compound_tag_new_named("Data"));
    if (!data)
        return;

    mcp_compound_tag_set_long(data.get(), "DayTime", time_);
    mcp_compound_tag_set_int(data.get(), "GameType", 0);
    mcp_compound_tag_set_string(data.get(), "generatorName", generator_name_.c_str());
    mcp_compound_tag_set_string(data.get(), "generatorOptions", "");
    mcp_compound_tag_set_int(data.get(), "generatorVersion", 1);
    mcp_compound_tag_set_byte(data.get(), "hardcore", 0);
    mcp_compound_tag_set_byte(data.get(), "initialized", 1);
    mcp_compound_tag_set_long(data.get(), "LastPlayed", current_unix_seconds());
    // LevelName = last path component (e.g. "worlds/world" → "world").
    {
        std::string lname;
        if (!path_.empty()) {
            size_t pos = path_.find_last_of('/');
            if (pos == std::string::npos)
                lname = path_;
            else
                lname = path_.substr(pos + 1);
        }
        mcp_compound_tag_set_string(data.get(), "LevelName", lname.c_str());
    }
    mcp_compound_tag_set_byte(data.get(), "raining", 0);
    mcp_compound_tag_set_int(data.get(), "rainTime", 0);
    mcp_compound_tag_set_long(data.get(), "RandomSeed", seed_);
    mcp_compound_tag_set_int(data.get(), "SpawnX", (int32_t)spawn_.x);
    mcp_compound_tag_set_int(data.get(), "SpawnY", (int32_t)spawn_.y);
    mcp_compound_tag_set_int(data.get(), "SpawnZ", (int32_t)spawn_.z);
    mcp_compound_tag_set_byte(data.get(), "thundering", 0);
    mcp_compound_tag_set_int(data.get(), "thunderTime", 0);
    mcp_compound_tag_set_int(data.get(), "version", 19133);
    mcp_compound_tag_set_long(data.get(), "Time", time_);
    mcp_compound_tag_set_long(data.get(), "SizeOnDisk", 0);

    // GameRules (empty compound).
    utils::TagGuard game_rules(mcp_compound_tag_new_named("GameRules"));
    mcp_compound_tag_set(data.get(), "GameRules", game_rules.release());

    mcp_compound_tag_set(root.get(), "Data", data.release());

    // Serialize big-endian.
    size_t nbt_len = 0;
    utils::BufferPtr nbt_buf(mcp_nbt_write_bytes(root.get(), /*little_endian=*/false, &nbt_len));
    if (!nbt_buf || nbt_len == 0)
        return;

    // GZIP-compress.
    std::vector<uint8_t> gz;
    bool ok = gzip_compress(nbt_buf.get(), nbt_len, &gz);
    if (!ok) {
        utils::MainLogger::get().warn("[Anvil] gzip compress failed for level.dat");
        return;
    }

    std::string p = path_ + "/level.dat";
    if (!write_file(p, gz.data(), gz.size())) {
        utils::MainLogger::get().warn("[Anvil] Failed to write level.dat");
    }
}

// ============================================================================
// Static API
// ============================================================================
bool Anvil::isValid(const std::string& path) {
    struct stat st{};
    if (::stat((path + "/level.dat").c_str(), &st) != 0)
        return false;
    if (!S_ISREG(st.st_mode))
        return false;
    if (::stat((path + "/region").c_str(), &st) != 0)
        return false;
    if (!S_ISDIR(st.st_mode))
        return false;
    // Every file in region/ must end in .mca.
    DIR* dir = ::opendir((path + "/region").c_str());
    if (!dir)
        return false;
    struct dirent* ent;
    bool all_mca = true;
    while ((ent = ::readdir(dir)) != nullptr) {
        std::string name = ent->d_name;
        if (name == "." || name == "..")
            continue;
        if (name.size() < 4 || name.substr(name.size() - 4) != ".mca") {
            all_mca = false;
            break;
        }
    }
    ::closedir(dir);
    return all_mca;
}

bool Anvil::generate(const std::string& path, const std::string& name, int64_t seed,
                     const std::string& generatorName) {
    if (!ensure_dir(path))
        return false;
    if (!ensure_dir(path + "/region"))
        return false;

    // Build Data compound.
    utils::TagGuard data(mcp_compound_tag_new_named("Data"));
    if (!data)
        return false;
    utils::TagGuard game_rules(mcp_compound_tag_new_named("GameRules"));
    mcp_compound_tag_set(data.get(), "GameRules", game_rules.release());

    mcp_compound_tag_set_long(data.get(), "DayTime", 0);
    mcp_compound_tag_set_int(data.get(), "GameType", 0);
    mcp_compound_tag_set_string(data.get(), "generatorName", generatorName.c_str());
    mcp_compound_tag_set_string(data.get(), "generatorOptions", "");
    mcp_compound_tag_set_int(data.get(), "generatorVersion", 1);
    mcp_compound_tag_set_byte(data.get(), "hardcore", 0);
    mcp_compound_tag_set_byte(data.get(), "initialized", 1);
    mcp_compound_tag_set_long(data.get(), "LastPlayed", current_unix_seconds());
    mcp_compound_tag_set_string(data.get(), "LevelName", name.c_str());
    mcp_compound_tag_set_byte(data.get(), "raining", 0);
    mcp_compound_tag_set_int(data.get(), "rainTime", 0);
    mcp_compound_tag_set_long(data.get(), "RandomSeed", seed);
    mcp_compound_tag_set_int(data.get(), "SpawnX", 128);
    mcp_compound_tag_set_int(data.get(), "SpawnY", 70);
    mcp_compound_tag_set_int(data.get(), "SpawnZ", 128);
    mcp_compound_tag_set_byte(data.get(), "thundering", 0);
    mcp_compound_tag_set_int(data.get(), "thunderTime", 0);
    mcp_compound_tag_set_int(data.get(), "version", 19133);
    mcp_compound_tag_set_long(data.get(), "Time", 0);
    mcp_compound_tag_set_long(data.get(), "SizeOnDisk", 0);

    utils::TagGuard root(mcp_compound_tag_new_named(""));
    mcp_compound_tag_set(root.get(), "Data", data.release());

    size_t nbt_len = 0;
    utils::BufferPtr nbt_buf(mcp_nbt_write_bytes(root.get(), /*little_endian=*/false, &nbt_len));
    if (!nbt_buf || nbt_len == 0)
        return false;

    std::vector<uint8_t> gz;
    bool ok = gzip_compress(nbt_buf.get(), nbt_len, &gz);
    if (!ok)
        return false;

    std::string level_dat = path + "/level.dat";
    return write_file(level_dat, gz.data(), gz.size());
}

} // namespace cursestone::world::format::anvil
