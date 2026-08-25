// SPDX-License-Identifier: LGPL-3.0-or-later
// Anvil.h — Nukkit Anvil LevelProvider, ported to AtomikPE's C++ core.
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

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "../../../main.h"
#include "../LevelProvider.h"
#include "AnvilRegionLoader.h"

namespace atomikpe::level::format::anvil {

class Anvil : public LevelProvider {
public:
    Anvil();
    ~Anvil() override;

    // ----- LevelProvider
    void             init(const std::string& path) override;
    void             shutdown() override;
    std::string      getName() const override { return "anvil"; }

    std::optional<std::shared_ptr<FullChunk>>
                     loadChunk(int32_t x, int32_t z) override;
    void             saveChunk(const std::shared_ptr<FullChunk>& chunk) override;
    void             unloadChunk(int32_t x, int32_t z) override;

    std::vector<std::pair<int32_t, int32_t>>
                     listChunks() const override;

    int64_t          getSeed()      const override { return seed_; }
    void             setSeed(int64_t s) override { seed_ = s; }
    int64_t          getCurrentTime() const override { return time_; }
    void             setCurrentTime(int64_t t) override { time_ = t; }
    math::Vector3    getSpawn() const override { return spawn_; }
    void             setSpawn(const math::Vector3& v) override { spawn_ = v; }

    void             setGenerator(generator::Generator* g) override { generator_ = g; }
    generator::Generator* getGenerator() const override { return generator_; }

    // ----- Anvil-specific surface

    // Path to the .mca region file for chunk (cx, cz):
    //   "<worldDir>/region/r.<rx>.<rz>.mca"
    std::string      regionPath(int32_t cx, int32_t cz) const;

    // Get-or-open the RegionLoader for the region containing (cx, cz).
    // Returns nullptr on I/O error. `create` controls whether a new
    // .mca file is created if it doesn't exist (used by saveChunk).
    AnvilRegionLoader* regionFor(int32_t cx, int32_t cz, bool create);

    // Static detection — used by future auto-detect logic. Returns true
    // if `path` looks like an Anvil world (level.dat present, region/
    // dir exists, all region files end in .mca).
    static bool      isValid(const std::string& path);

    // Static generator — creates the on-disk scaffolding for a fresh
    // Anvil world at `path` (writes level.dat with NBT Data compound +
    // creates region/ folder). Mirrors Nukkit's Anvil.generate().
    static bool      generate(const std::string& path,
                                const std::string& name,
                                int64_t seed,
                                const std::string& generatorName);

private:
    // Load level.dat (GZIP-compressed big-endian) → sets seed_/time_/spawn_.
    void             loadMetadata();
    // Save level.dat with current metadata.
    void             saveMetadata();
    // Evict the least-recently-used region file from `regions_`.
    void             evictOldestRegion();

    std::string                     path_;
    int64_t                         seed_ = 0;
    int64_t                         time_ = 0;
    math::Vector3                   spawn_{0.0, 64.0, 0.0};
    std::string                     generator_name_ = "normal";
    generator::Generator*           generator_ = nullptr;

    static constexpr size_t kMaxOpenRegions = 8;
    std::unordered_map<std::string, std::unique_ptr<AnvilRegionLoader>> regions_;
    std::vector<std::string> region_lru_;
};

struct AnvilRegistrar {
    AnvilRegistrar();
};
extern AnvilRegistrar g_anvil_registrar_;

} // namespace atomikpe::level::format::anvil
