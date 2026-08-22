// SPDX-License-Identifier: LGPL-3.0-or-later
// McaCpp — the default MCPPEServer world format. C++.
//
// `mcacpp` is a region-file format (32×32 chunks per .mcacpp file)
// designed for fast random chunk access and easy portability across
// C/C++ implementations. The on-disk spec lives in
// /home/z/my-project/work/MCPPEServer/docs/MCACPP_FORMAT.md.
//
// Chunk data on disk is zlib-compressed NBT (Java big-endian) so
// existing NBT code can read/write chunks without a separate
// little-endian path.
//
// Region-file cache: McaCpp keeps an LRU of open region FILE*'s
// (default 8) to amortize the open/read/close cycle across
// neighbouring chunk requests (which usually share a region).
//
// Phase 8 status: the public API matches LevelProvider. I/O is
// implemented at the "format header + chunk-table walk" level; full
// zlib + NBT integration arrives when the entity / tile layers land
// (Phases 13-14).
#pragma once

#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include "../../../main.h"
#include "../LevelProvider.h"
#include "../Chunk.h"

namespace mcppeserver::level::format::mcacpp {

// One open region file. The header + chunk table are read once on
// open and cached; chunk payloads are read on demand.
struct RegionFile {
    std::string path;
    FILE*       fp = nullptr;
    int32_t     region_x = 0;
    int32_t     region_z = 0;
    bool        writable = false;
    // Chunk table: 1024 entries, each holding a 24-bit sector offset
    // and an 8-bit sector count. A zero offset means "not stored".
    std::array<uint32_t, 1024> table{};

    // Compute the index into `table` for a chunk within this region.
    // `cx` / `cz` are *region-local* chunk coords (0..31).
    static inline size_t tableIndex(int cx, int cz) {
        return (size_t)((cx & 31) + (cz & 31) * 32);
    }
};

class McaCpp : public LevelProvider {
public:
    McaCpp();
    ~McaCpp() override;

    // LevelProvider interface.
    void             init(const std::string& path) override;
    void             shutdown() override;
    std::string      getName() const override { return "mcacpp"; }

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

    // ----- mcacpp-specific surface

    // Path to the region file for chunk (cx, cz). `cx` / `cz` are
    // absolute chunk coords; we floor-divide by 32 to find the region.
    std::string      regionPath(int32_t cx, int32_t cz) const;

    // Open or re-use a cached region file. Returns nullptr on I/O
    // failure. The returned pointer is owned by McaCpp; do not close.
    RegionFile*      regionFor(int32_t cx, int32_t cz, bool create);

    // Read a raw chunk payload (decompressed NBT bytes) for chunk
    // (cx, cz). Returns nullopt if the chunk is not stored.
    std::optional<std::vector<uint8_t>>
                     readChunkPayload(int32_t cx, int32_t cz);

    // Write a raw chunk payload to disk. `compression` follows the
    // mcacpp format: 0=none, 1=zlib, 2=lz4. `payload` is the
    // uncompressed NBT. McaCpp handles compression + sector
    // allocation.
    bool             writeChunkPayload(int32_t cx, int32_t cz,
                                          uint8_t compression,
                                          const std::vector<uint8_t>& payload);

private:
    std::string                     path_;       // world root dir, e.g. "worlds/world"
    int64_t                         seed_ = 0;
    int64_t                         time_ = 0;
    math::Vector3                   spawn_{0, 64, 0};
    generator::Generator*           generator_ = nullptr;

    // Open-region cache (path -> RegionFile). Capped at
    // kMaxOpenRegions_ via LRU eviction.
    static constexpr size_t kMaxOpenRegions_ = 8;
    std::unordered_map<std::string, std::unique_ptr<RegionFile>> regions_;

    // Read the 256-byte header + 4096-byte chunk table from `fp`.
    // Returns false on I/O error or bad magic.
    static bool      readRegionHeader(FILE* fp, int32_t* out_rx,
                                        int32_t* out_rz,
                                        std::array<uint32_t, 1024>* out_table);

    // Flush the chunk table back to disk. Called after every write.
    static bool      writeRegionTable(FILE* fp, int32_t rx, int32_t rz,
                                         const std::array<uint32_t, 1024>& table);

    // Allocate `sectorCount` sectors in `rf`, returning the offset
    // (in 4096-byte sectors from file start). Grows the file as
    // needed. Re-uses any free sectors tracked in the table (TBD:
    // for Phase 8 we just append — defragmentation is a v1.1 item).
    uint32_t        allocSectors(RegionFile& rf, uint8_t sectorCount);

    // Close + drop the least-recently-used region file from the
    // cache, freeing up a slot.
    void            evictOldestRegion();

    // Load seed + spawn + time from the per-world metadata file
    // (path_/level.dat — NBT). On missing file, defaults are used.
    void            loadMetadata();
    void            saveMetadata();
};

// Auto-registration helper. Lives in an anonymous namespace in the
// .cpp file. Called once at process start.
struct McaCppRegistrar {
    McaCppRegistrar();
};
extern McaCppRegistrar g_mcacpp_registrar_;

} // namespace mcppeserver::level::format::mcacpp
