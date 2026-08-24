// SPDX-License-Identifier: LGPL-3.0-or-later
// AtmkMca.cpp — AtmkMca world format provider implementation.
#include "AtmkMca.h"
#include "AtmkChunkEncoder.h"
#include "AtmkChunkDecoder.h"
#include "../LevelProviderManager.h"
#include "../ChunkAdapter.h"
#include "../Chunk.h"
#include "../../../nbt/NBT.h"
#include "../../../nbt/tag/CompoundTag.h"
#include "../../../nbt/tag/Tag.h"
#include "../../../utils/MainLogger.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <zlib.h>

namespace atomikpe::level::format::atmkma {

// ----- Auto-registration with LevelProviderManager.
AtmkMcaRegistrar g_atmkma_registrar_;
AtmkMcaRegistrar::AtmkMcaRegistrar() {
    LevelProviderManager::getInstance().registerProvider(
        "atmkma", [](){ return std::unique_ptr<LevelProvider>(new AtmkMca()); });
    // Backward-compat aliases for old configs that may still reference
    // "mcacpp" or "mcanvil".
    LevelProviderManager::getInstance().registerProvider(
        "mcacpp", [](){ return std::unique_ptr<LevelProvider>(new AtmkMca()); });
    LevelProviderManager::getInstance().registerProvider(
        "mcanvil", [](){ return std::unique_ptr<LevelProvider>(new AtmkMca()); });
}

// ----- Helpers -----
static bool ensure_dir(const std::string& path) {
    struct stat st{};
    if (::stat(path.c_str(), &st) == 0) {
        return S_ISDIR(st.st_mode);
    }
    if (errno != ENOENT) return false;
    return ::mkdir(path.c_str(), 0775) == 0;
}

// zlib compress helper (level 7).
static bool zlib_compress(const uint8_t* src, size_t src_len,
                            std::vector<uint8_t>* out) {
    uLongf bound = compressBound((uLong)src_len);
    out->resize(bound);
    uLongf dest_len = bound;
    int rc = compress2(out->data(), &dest_len, src, (uLong)src_len, 7);
    if (rc != Z_OK) {
        out->clear();
        return false;
    }
    out->resize(dest_len);
    return true;
}

static int64_t current_millis() {
    return (int64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

// ----- Lifecycle -----
AtmkMca::AtmkMca() = default;

AtmkMca::~AtmkMca() { shutdown(); }

void AtmkMca::init(const std::string& path) {
    path_ = path;
    // Create directories: <world>/ and <world>/regions/
    ensure_dir(path_);
    ensure_dir(path_ + "/regions");
    loadMetadata();
}

void AtmkMca::shutdown() {
    // Flush + close every open region file.
    for (auto& [_, rf_ptr] : regions_) {
        if (rf_ptr) rf_ptr->close();
    }
    regions_.clear();
    region_lru_.clear();
    // Persist level.dat metadata on shutdown.
    saveMetadata();
}

// ----- Region file management -----
std::string AtmkMca::regionPath(int32_t cx, int32_t cz) const {
    int32_t rx = (cx >= 0) ? (cx / 32) : ((cx - 31) / 32);
    int32_t rz = (cz >= 0) ? (cz / 32) : ((cz - 31) / 32);
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%s/regions/r.%d.%d.atmka",
                   path_.c_str(), (int)rx, (int)rz);
    return std::string(buf);
}

AtmkRegionFile* AtmkMca::regionFor(int32_t cx, int32_t cz, bool create) {
    std::string p = regionPath(cx, cz);
    auto it = regions_.find(p);
    if (it != regions_.end()) {
        // Move to back of LRU.
        auto lru_it = std::find(region_lru_.begin(), region_lru_.end(), p);
        if (lru_it != region_lru_.end()) {
            region_lru_.erase(lru_it);
            region_lru_.push_back(p);
        }
        return it->second.get();
    }
    // Evict if at capacity.
    if (regions_.size() >= kMaxOpenRegions) {
        evictOldestRegion();
    }
    int32_t rx = (cx >= 0) ? (cx / 32) : ((cx - 31) / 32);
    int32_t rz = (cz >= 0) ? (cz / 32) : ((cz - 31) / 32);
    auto rf = std::make_unique<AtmkRegionFile>();
    if (!rf->open(p, rx, rz, create)) {
        return nullptr;
    }
    AtmkRegionFile* raw = rf.get();
    regions_[p] = std::move(rf);
    region_lru_.push_back(p);
    return raw;
}

void AtmkMca::evictOldestRegion() {
    if (region_lru_.empty()) return;
    std::string oldest = region_lru_.front();
    region_lru_.erase(region_lru_.begin());
    auto it = regions_.find(oldest);
    if (it != regions_.end()) {
        if (it->second) it->second->close();
        regions_.erase(it);
    }
}

// ----- Chunk I/O -----
std::optional<std::shared_ptr<FullChunk>>
AtmkMca::loadChunk(int32_t x, int32_t z) {
    AtmkRegionFile* rf = regionFor(x, z, /*create=*/false);
    if (!rf) return std::nullopt;

    int32_t local_x = x & 31;
    int32_t local_z = z & 31;
    auto payload = rf->readChunk(local_x, local_z);
    if (!payload) return std::nullopt;

    // Decode NBT little-endian into mcp_chunk_t*.
    mcp_chunk_t* raw = atmk_chunk_decode(payload->data(), payload->size());
    if (!raw) return std::nullopt;

    // Wrap in ChunkAdapter (takes ownership).
    auto adapter = std::make_shared<ChunkAdapter>(raw);
    return adapter;
}

void AtmkMca::saveChunk(const std::shared_ptr<FullChunk>& chunk) {
    if (!chunk) return;

    auto* adapter = dynamic_cast<ChunkAdapter*>(chunk.get());
    if (!adapter) return;
    mcp_chunk_t* raw = adapter->chunk();
    if (!raw) return;

    // 1. Encode chunk to NBT (little-endian).
    size_t nbt_len = 0;
    uint8_t* nbt_buf = atmk_chunk_encode(raw, &nbt_len);
    if (!nbt_buf || nbt_len == 0) {
        utils::MainLogger::get().warn("AtmkMca: failed to encode chunk "
            + std::to_string(raw->x) + "," + std::to_string(raw->z));
        return;
    }

    // 2. Zlib-compress.
    std::vector<uint8_t> compressed;
    if (!zlib_compress(nbt_buf, nbt_len, &compressed)) {
        free(nbt_buf);
        utils::MainLogger::get().warn("AtmkMca: zlib compress failed");
        return;
    }
    free(nbt_buf);

    // 3. Write to region file.
    AtmkRegionFile* rf = regionFor(raw->x, raw->z, /*create=*/true);
    if (!rf) {
        utils::MainLogger::get().warn("AtmkMca: failed to open region for chunk "
            + std::to_string(raw->x) + "," + std::to_string(raw->z));
        return;
    }
    int32_t local_x = raw->x & 31;
    int32_t local_z = raw->z & 31;
    if (!rf->writeChunk(local_x, local_z, /*compression=*/1, compressed)) {
        utils::MainLogger::get().warn("AtmkMca: failed to write chunk "
            + std::to_string(raw->x) + "," + std::to_string(raw->z));
        return;
    }

    // 4. Mark chunk as clean.
    raw->dirty = false;
    raw->last_update = current_millis();
}

void AtmkMca::unloadChunk(int32_t /*x*/, int32_t /*z*/) {
    // In-memory chunk cache is owned by Level, not by the provider.
    // No-op here.
}

std::vector<std::pair<int32_t, int32_t>>
AtmkMca::listChunks() const {
    // Walk every .atmka file in <world>/regions/ and collect chunk coords.
    std::vector<std::pair<int32_t, int32_t>> out;
    std::string regions_dir = path_ + "/regions";
    DIR* dir = ::opendir(regions_dir.c_str());
    if (!dir) return out;
    struct dirent* ent;
    while ((ent = ::readdir(dir)) != nullptr) {
        std::string name = ent->d_name;
        if (name.size() < 12) continue;
        if (name.substr(0, 2) != "r.") continue;
        if (name.substr(name.size() - 6) != ".atmka") continue;
        // Parse: r.RX.RZ.atmka
        // Find first and second dot after "r.".
        size_t first_dot = name.find('.', 2);
        if (first_dot == std::string::npos) continue;
        size_t second_dot = name.find('.', first_dot + 1);
        if (second_dot == std::string::npos) continue;
        std::string rx_str = name.substr(2, first_dot - 2);
        std::string rz_str = name.substr(first_dot + 1, second_dot - first_dot - 1);
        int32_t rx = 0, rz = 0;
        try {
            rx = std::stoi(rx_str);
            rz = std::stoi(rz_str);
        } catch (...) { continue; }
        // Open the region read-only to list its chunks.
        std::string full_path = regions_dir + "/" + name;
        // We need to mutate the cache, so const_cast.
        auto* self = const_cast<AtmkMca*>(this);
        AtmkRegionFile* rf = self->regionFor(rx * 32, rz * 32, /*create=*/false);
        if (!rf) continue;
        auto chunk_list = rf->listChunks();
        out.insert(out.end(), chunk_list.begin(), chunk_list.end());
    }
    ::closedir(dir);
    return out;
}

// ----- Metadata (level.dat) -----
void AtmkMca::loadMetadata() {
    std::string p = path_ + "/level.dat";
    FILE* fp = std::fopen(p.c_str(), "rb");
    if (!fp) {
        seed_  = 0;
        time_  = 0;
        spawn_ = math::Vector3{0.0, 64.0, 0.0};
        return;
    }
    std::fseek(fp, 0, SEEK_END);
    long sz = std::ftell(fp);
    std::fseek(fp, 0, SEEK_SET);
    if (sz <= 0) { std::fclose(fp); return; }
    std::vector<uint8_t> buf(sz);
    if (std::fread(buf.data(), 1, sz, fp) != (size_t)sz) {
        std::fclose(fp);
        return;
    }
    std::fclose(fp);

    mcp_tag_t* root = mcp_nbt_read_bytes(buf.data(), buf.size(),
                                            /*little_endian=*/true);
    if (!root || mcp_tag_get_type(root) != MCP_TAG_COMPOUND) {
        if (root) mcp_tag_destroy(root);
        return;
    }
    seed_  = mcp_compound_tag_get_long(root, "RandomSeed", 0);
    time_  = mcp_compound_tag_get_long(root, "Time", 0);
    const mcp_tag_t* spawn = mcp_compound_tag_get_compound(root, "Spawn");
    if (spawn) {
        spawn_.x = (double)mcp_compound_tag_get_int(spawn, "x", 0);
        spawn_.y = (double)mcp_compound_tag_get_int(spawn, "y", 64);
        spawn_.z = (double)mcp_compound_tag_get_int(spawn, "z", 0);
    }
    mcp_tag_destroy(root);
}

void AtmkMca::saveMetadata() {
    mcp_tag_t* root = mcp_compound_tag_new_named("");
    if (!root) return;
    mcp_compound_tag_set_long(root, "RandomSeed", seed_);
    mcp_compound_tag_set_long(root, "Time", time_);
    mcp_compound_tag_set_long(root, "LastPlayed", current_millis());

    // Spawn compound.
    mcp_tag_t* spawn = mcp_compound_tag_new_named("Spawn");
    mcp_compound_tag_set_int(spawn, "x", (int32_t)spawn_.x);
    mcp_compound_tag_set_int(spawn, "y", (int32_t)spawn_.y);
    mcp_compound_tag_set_int(spawn, "z", (int32_t)spawn_.z);
    mcp_compound_tag_set(root, "Spawn", spawn);

    size_t len = 0;
    uint8_t* data = mcp_nbt_write_bytes(root, /*little_endian=*/true, &len);
    mcp_tag_destroy(root);
    if (!data || len == 0) return;

    std::string p = path_ + "/level.dat";
    FILE* fp = std::fopen(p.c_str(), "wb");
    if (fp) {
        std::fwrite(data, 1, len, fp);
        std::fclose(fp);
    }
    free(data);
}

} // namespace atomikpe::level::format::atmkma
