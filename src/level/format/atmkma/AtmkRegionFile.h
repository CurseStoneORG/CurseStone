// SPDX-License-Identifier: LGPL-3.0-or-later
// AtmkRegionFile.h — single .atmka region file (32x32 chunks).
//
// Layout (see docs/ATMKA_FORMAT.md):
//   [0..256)            256-byte header (magic "ATMK" + version + ts + rx + rz)
//   [256..4352)         4096-byte chunk table (1024 entries × 4 bytes)
//   [4352..)            Variable-size chunk payloads (sectors of 4096 bytes)
//
// Each chunk table entry is packed:
//   bits 0..23   SectorOffset (24-bit, max 16M sectors = 64 GiB)
//   bits 24..31  SectorCount  (8-bit,  max 255 sectors = 1 MiB)
//   0 offset means the chunk is not stored.
//
// Each chunk payload on disk starts with:
//   [0..4)   uint32 BE CompressedLength  (excludes this header)
//   [4)      uint8   CompressionType (0=none, 1=zlib, 2=lz4)
//   [5..)    Compressed NBT bytes (padded to sector boundary with zeros)
#pragma once

#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace atomikpe::level::format::atmkma {

constexpr uint32_t kAtmkaMagic      = 0x41544D4Bu;   // "ATMK" (big-endian)
constexpr uint8_t  kAtmkaVersion    = 1;
constexpr size_t   kHeaderSize       = 256;
constexpr size_t   kTableSize        = 4096;
constexpr size_t   kTableEntries     = 1024;
constexpr size_t   kSectorSize       = 4096;
constexpr size_t   kMaxPayloadSize   = 1024 * 1024;   // hard cap: 1 MiB

class AtmkRegionFile {
public:
    AtmkRegionFile();
    ~AtmkRegionFile();

    // Open (or create) the region file at `path` for region (rx, rz).
    // `create=true` will create the file + write the header if it
    // doesn't exist. Returns false on I/O error.
    bool open(const std::string& path, int32_t rx, int32_t rz, bool create);

    // Close + flush. Idempotent.
    void close();

    // Read a chunk's decompressed NBT payload. Returns nullopt if the
    // chunk is not stored in this region.
    std::optional<std::vector<uint8_t>> readChunk(int32_t localCx, int32_t localCz);

    // Write a chunk's NBT payload (already-compressed by caller) to
    // the region file. Returns false on I/O error.
    bool writeChunk(int32_t localCx, int32_t localCz,
                       uint8_t compressionType,
                       const std::vector<uint8_t>& compressedPayload);

    // Returns true if chunk (localCx, localCz) is currently stored.
    bool hasChunk(int32_t localCx, int32_t localCz) const;

    // Iterate every chunk coord present in the region file. Returns
    // absolute chunk coords (region_x*32 + local_x, region_z*32 + local_z).
    std::vector<std::pair<int32_t, int32_t>> listChunks() const;

    int32_t regionX() const { return region_x_; }
    int32_t regionZ() const { return region_z_; }

    // Static helper: compute the index into the chunk table for a
    // region-local chunk coord.
    static inline size_t tableIndex(int32_t localCx, int32_t localCz) {
        return (size_t)((localCx & 31) + (localCz & 31) * 32);
    }

private:
    // Read 256-byte header + 4096-byte chunk table. Returns false on
    // I/O error or bad magic.
    bool readHeaderAndTable();

    // Flush the chunk table back to disk at offset 256.
    bool writeTable();

    // Allocate `sectorCount` consecutive sectors. Appends to the file
    // if no free sectors are tracked (Phase 1: simple append-only).
    uint32_t allocSectors(uint8_t sectorCount);

    // Bump the file size to a multiple of kSectorSize.
    void padToSectorBoundary();

    std::string                path_;
    FILE*                      fp_ = nullptr;
    int32_t                    region_x_ = 0;
    int32_t                    region_z_ = 0;
    bool                       writable_ = false;
    std::array<uint32_t, 1024> table_{};
    // Highest sector offset that has been allocated so far. Used by
    // allocSectors to know where to append next.
    uint32_t                   next_free_sector_ = 1;  // sector 0 is header+table
};

} // namespace atomikpe::level::format::atmkma
