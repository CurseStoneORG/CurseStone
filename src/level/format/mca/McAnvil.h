// SPDX-License-Identifier: LGPL-3.0-or-later
// McAnvil — Java Edition .mca (Anvil) world format. C++.
//
// The Java Edition Anvil format (`.mca`) is a region-file format
// with 32×32 chunks per file. Each chunk is 16×16 wide and 256
// tall (16 sub-chunks). The on-disk layout differs from mcacpp only
// in:
//   - Region file naming:    `r.<rx>.<rz>.mca` (no `chunks/` subdir).
//   - Sector size:            4096 bytes (same as mcacpp).
//   - Chunk payload:          zlib-compressed NBT, big-endian,
//                             compound root tag "Level".
//   - Compression byte:      1 = GZIP (legacy), 2 = zlib (modern).
//   - No magic + version:    the first 8 KiB of a region file is
//                             header + table only, no magic word.
//   - Chunk table layout:    3-byte offset (sectors) + 1-byte sector
//                             count — same packed layout as mcacpp
//                             but big-endian.
//
// Phase 8 ships a stub. Real I/O lands in Phase 12 when the NBT
// codec is in place (so we can decode the legacy Java Anvil NBT
// into the same `mcp_chunk_t` representation mcacpp uses).
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "../../../main.h"
#include "../LevelProvider.h"

namespace cenisyspe::level::format::mca {

class McAnvil : public LevelProvider {
public:
    McAnvil();
    ~McAnvil() override;

    void             init(const std::string& path) override;
    void             shutdown() override;
    std::string      getName() const override { return "mca"; }

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

    // ----- McAnvil-specific surface

    // Region file path for chunk (cx, cz). Java uses
    // `<world>/region/r.<rx>.<rz>.mca`.
    std::string      regionPath(int32_t cx, int32_t cz) const;

private:
    std::string                   path_;
    int64_t                       seed_ = 0;
    int64_t                       time_ = 0;
    math::Vector3                 spawn_{0.0, 64.0, 0.0};
    generator::Generator*         generator_ = nullptr;
};

struct McAnvilRegistrar {
    McAnvilRegistrar();
};
extern McAnvilRegistrar g_mca_registrar_;

} // namespace cenisyspe::level::format::mca
