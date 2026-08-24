// SPDX-License-Identifier: LGPL-3.0-or-later
// AtmkMca.h — AtmkMca world format provider (replaces McaCpp entirely).
//
// AtmkMca is AtomikPE's own region-file format (.atmka), optimized for
// MCPE 0.15.x. It features:
//   - 256-byte header with magic "ATMK" + version
//   - 4096-byte chunk table (1024 entries × 4 bytes)
//   - Sectors of 4096 bytes
//   - Zlib-compressed chunk payloads
//   - Complete chunk NBT encoding (terrain + tiles + entities)
//
// This provider delegates chunk encoding/decoding to AtmkChunkEncoder
// and AtmkChunkDecoder, and delegates region file I/O to AtmkRegionFile.
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include "../../../main.h"
#include "../LevelProvider.h"
#include "AtmkRegionFile.h"

namespace atomikpe::level::format::atmkma {

class AtmkMca : public LevelProvider {
public:
    AtmkMca();
    ~AtmkMca() override;

    // LevelProvider interface.
    void             init(const std::string& path) override;
    void             shutdown() override;
    std::string      getName() const override { return "atmkma"; }

    std::optional<std::shared_ptr<FullChunk>>
                     loadChunk(int32_t x, int32_t z) override;
    void             saveChunk(const std::shared_ptr<FullChunk>& chunk) override;
    void             unloadChunk(int32_t x, int32_t z) override;

    std::vector<std::pair<int32_t, int32_t>>
                     listChunks() const override;

    int64_t          getSeed()      const override { return seed_; }
    int64_t          getCurrentTime() const override { return time_; }
    void             setCurrentTime(int64_t t) override { time_ = t; }
    math::Vector3    getSpawn() const override { return spawn_; }
    void             setSpawn(const math::Vector3& v) override { spawn_ = v; }

    void             setGenerator(generator::Generator* g) override { generator_ = g; }
    generator::Generator* getGenerator() const override { return generator_; }

    // Path to the region file for chunk (cx, cz).
    std::string      regionPath(int32_t cx, int32_t cz) const;

    // Open or re-use a cached region file. Returns nullptr on I/O error.
    AtmkRegionFile*  regionFor(int32_t cx, int32_t cz, bool create);

private:
    std::string                     path_;       // world root dir, e.g. "worlds/world"
    int64_t                         seed_ = 0;
    int64_t                         time_ = 0;
    math::Vector3                   spawn_{0, 64, 0};
    generator::Generator*           generator_ = nullptr;

    // Open-region cache (path -> AtmkRegionFile). Capped at kMaxOpenRegions.
    static constexpr size_t kMaxOpenRegions = 8;
    std::unordered_map<std::string, std::unique_ptr<AtmkRegionFile>> regions_;
    // LRU ordering (oldest at front).
    std::vector<std::string> region_lru_;

    // Load/save level.dat metadata.
    void            loadMetadata();
    void            saveMetadata();
    void            evictOldestRegion();
};

// Auto-registration helper.
struct AtmkMcaRegistrar {
    AtmkMcaRegistrar();
};
extern AtmkMcaRegistrar g_atmkma_registrar_;

} // namespace atomikpe::level::format::atmkma
