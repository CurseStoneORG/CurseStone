// SPDX-License-Identifier: LGPL-3.0-or-later
// AtmkRegionFile.h — single .atmka region file (32x32 chunks).
#pragma once
#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace atomikpe::level::format::atmkma {

constexpr uint32_t kAtmkaMagic      = 0x41544D4Bu;   // "ATMK"
constexpr uint8_t  kAtmkaVersion    = 1;
constexpr size_t   kHeaderSize       = 256;
constexpr size_t   kTableSize        = 4096;
constexpr size_t   kTableEntries     = 1024;
constexpr size_t   kSectorSize       = 4096;
constexpr size_t   kMaxPayloadSize   = 1024 * 1024;

class AtmkRegionFile {
public:
    AtmkRegionFile();
    ~AtmkRegionFile();
    bool open(const std::string& path, int32_t rx, int32_t rz, bool create);
    void close();
    std::optional<std::vector<uint8_t>> readChunk(int32_t localCx, int32_t localCz);
    bool writeChunk(int32_t localCx, int32_t localCz,
                       uint8_t compressionType,
                       const std::vector<uint8_t>& compressedPayload);
    bool hasChunk(int32_t localCx, int32_t localCz) const;
    std::vector<std::pair<int32_t, int32_t>> listChunks() const;
    int32_t regionX() const { return region_x_; }
    int32_t regionZ() const { return region_z_; }
    static inline size_t tableIndex(int32_t localCx, int32_t localCz) {
        return (size_t)((localCx & 31) + (localCz & 31) * 32);
    }
private:
    bool readHeaderAndTable();
    bool writeTable();
    uint32_t allocSectors(uint8_t sectorCount);
    std::string                path_;
    FILE*                      fp_ = nullptr;
    int32_t                    region_x_ = 0;
    int32_t                    region_z_ = 0;
    bool                       writable_ = false;
    std::array<uint32_t, 1024> table_{};
    uint32_t                   next_free_sector_ = 1;
};

} // namespace atomikpe::level::format::atmkma
