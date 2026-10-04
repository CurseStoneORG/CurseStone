// SPDX-License-Identifier: LGPL-3.0-or-later
// AnvilRegionLoader.h — single .mca region file (32x32 chunks).
//
// Ported from Nukkit InfinityEdition's
// `cn.nukkit.level.format.anvil.RegionLoader` and
// `cn.nukkit.level.format.generic.BaseRegionLoader` to C++.
//
// File layout (vanilla Minecraft Java / Nukkit Anvil):
//   [   0 .. 4095]  Location table  — 1024 entries × 4 bytes BE
//                   Each entry: (sectorOffset << 8) | sectorCount
//                   offset == 0  → chunk not present
//   [4096 .. 8191]  Timestamp table — 1024 entries × 4 bytes BE
//                   Unix-seconds of last write per chunk slot.
//   [8192 ..]       Chunk sectors (each 4096 bytes).
//                   Each chunk: [4-byte length BE][1-byte compression][payload]
//                   compression: 1 = GZIP, 2 = ZLIB (Nukkit always writes ZLIB).
//
// The chunk-slot index inside a region for a local (lx, lz) is
//   index = lx + (lz << 5)   (= z * 32 + x).
#pragma once

#include <array>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

namespace cursestone::world::format::anvil {

constexpr int32_t kRegionSize        = 32;     // 32x32 chunks per region
constexpr int32_t kChunkSlotCount    = 1024;   // 32 * 32
constexpr size_t  kSectorSize        = 4096;
constexpr size_t  kLocationTableSize = 4096;   // 1024 * 4
constexpr size_t  kTimestampTableSize = 4096;
constexpr size_t  kHeaderSize        = kLocationTableSize + kTimestampTableSize; // 8192
constexpr size_t  kMaxChunkSectors   = 256;    // 256 * 4096 = 1 MiB cap
constexpr uint8_t kCompressionGzip   = 1;
constexpr uint8_t kCompressionZlib   = 2;

struct LocationEntry {
    uint32_t offset = 0;     // in sectors (from start of file)
    uint8_t  count   = 0;     // in sectors
    uint32_t timestamp = 0;   // unix seconds
};

class AnvilRegionLoader {
public:
    AnvilRegionLoader();
    ~AnvilRegionLoader();

    // Open (or create) the region file at `path` for region (rx, rz).
    // On a brand-new file, writes a blank 8 KiB header (location table
    // all-zero, timestamp table filled with current unix time).
    bool open(const std::string& path, int32_t rx, int32_t rz);
    void close();

    // Read the chunk payload bytes at local (lx, lz).
    // Returns std::nullopt if the slot is empty / corrupt.
    // The returned bytes are the *compressed* chunk payload (without
    // the 4-byte length / 1-byte compression prefix); callers must
    // inflate (ZLIB) before NBT decoding.
    std::optional<std::vector<uint8_t>>
    readChunk(int32_t lx, int32_t lz);

    // Write compressed payload at slot (lx, lz). The compression byte
    // is written by this function; `payload` is the already-compressed
    // bytes (ZLIB). Returns false on I/O error or chunk-too-big.
    bool writeChunk(int32_t lx, int32_t lz, uint8_t compression,
                    const std::vector<uint8_t>& payload);

    bool        hasChunk(int32_t lx, int32_t lz) const;
    int32_t     regionX() const { return region_x_; }
    int32_t     regionZ() const { return region_z_; }
    long        lastUsed() const { return last_used_; }

    // Compute the chunk-slot index from local (lx, lz).
    static inline size_t slotIndex(int32_t lx, int32_t lz) {
        return (size_t)((lx & 31) + ((lz & 31) << 5));
    }

private:
    bool loadLocationTable();
    bool writeLocationTable();
    bool writeLocationIndex(size_t index);
    bool createBlank();

    std::string                path_;
    FILE*                      fp_ = nullptr;
    int32_t                    region_x_ = 0;
    int32_t                    region_z_ = 0;
    uint32_t                   last_sector_ = 1;  // next free sector (1-based; 0 = header)
    std::array<LocationEntry, kChunkSlotCount> table_{};
    long                       last_used_ = 0;
};

} // namespace cursestone::world::format::anvil
