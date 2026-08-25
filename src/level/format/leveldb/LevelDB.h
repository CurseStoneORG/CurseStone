// SPDX-License-Identifier: LGPL-3.0-or-later
// LevelDB — MCPE's default on-disk format (LevelDB-backed). C++.
//
// MCPE 0.15.x shipped with chunks stored in a per-world LevelDB.
// Keys follow the format `chunkX:chunkZ:_type` where `_type` is one
// of:
//   0x76  "v"  full chunk data (legacy)
//   0x30  "0"  block data + tile entities (newer)
//   0x31  "1"  biome + state (newer)
//   0x32  "2"  entity list (newer)
//   0x33  "3"  block-extra-data (newer)
//   0x34  "4"  heightmap + 2D block palette (newer)
//
// Full LevelDB support requires linking against Google's leveldb
// library. Phase 8 ships a stub that compiles cleanly with
// ATOMIKPE_LEVELDB=0 and returns "chunk not on disk" for every
// request. When the build finds leveldb (via cmake/FindLevelDB.cmake
// and ATOMIKPE_LEVELDB=1), a real backend lands in Phase 12+.
//
// Design rationale: keeping the API identical to anvil/Anvil means
// Level::load() can pick a provider by name without format-specific
// special cases; the stub is the no-op implementation of that API.
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "../../../main.h"
#include "../LevelProvider.h"

namespace atomikpe::level::format::leveldb {

class LevelDB : public LevelProvider {
public:
    LevelDB();
    ~LevelDB() override;

    void             init(const std::string& path) override;
    void             shutdown() override;
    std::string      getName() const override { return "leveldb"; }

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

    // Stub — LevelDB doesn't use level.dat in the same way.
    void             saveLevelData() override {}

    // ----- LevelDB-specific surface

    // Build the LevelDB key for the chunk at (cx, cz) of the given
    // record type (one of "v" / "0" / "1" / "2" / "3" / "4" above).
    // The returned bytes are ready to be passed to leveldb_get.
    static std::vector<uint8_t> chunkKey(int32_t cx, int32_t cz, uint8_t type);

    // Whether the on-disk LevelDB library was linked in. False in
    // Stub — callers should fall back to anvil.
    static bool     hasLevelDB();

private:
    std::string                   path_;
    int64_t                       seed_ = 0;
    int64_t                       time_ = 0;
    math::Vector3                 spawn_{0.0, 64.0, 0.0};
    generator::Generator*         generator_ = nullptr;
};

struct LevelDBRegistrar {
    LevelDBRegistrar();
};
extern LevelDBRegistrar g_leveldb_registrar_;

} // namespace atomikpe::level::format::leveldb
