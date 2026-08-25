// SPDX-License-Identifier: LGPL-3.0-or-later
// AtmkRegionFile.cpp — single .atmka region file implementation.
#include "AtmkRegionFile.h"
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <ctime>
#include <sys/stat.h>
#include <sys/types.h>
#include <zlib.h>

namespace atomikpe::level::format::atmkma {

static void write_be32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);  p[3] = (uint8_t)(v);
}
static uint32_t read_be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  | (uint32_t)p[3];
}

AtmkRegionFile::AtmkRegionFile() = default;
AtmkRegionFile::~AtmkRegionFile() { close(); }

bool AtmkRegionFile::open(const std::string& path, int32_t rx, int32_t rz, bool create) {
    path_ = path;
    region_x_ = rx;
    region_z_ = rz;
    writable_ = create;

    fp_ = std::fopen(path.c_str(), "r+b");
    if (!fp_ && create) {
        fp_ = std::fopen(path.c_str(), "w+b");
    }
    if (!fp_) return false;

    std::fseek(fp_, 0, SEEK_END);
    long sz = std::ftell(fp_);
    if (sz == 0) {
        // New file: write header + empty table.
        std::fseek(fp_, 0, SEEK_SET);
        uint8_t header[kHeaderSize] = {0};
        header[0] = 'A'; header[1] = 'T'; header[2] = 'M'; header[3] = 'K';
        header[4] = kAtmkaVersion;
        int64_t ts = (int64_t)std::time(nullptr);
        for (int i = 0; i < 8; ++i) {
            header[5 + i] = (uint8_t)((ts >> ((7 - i) * 8)) & 0xFF);
        }
        write_be32(header + 13, (uint32_t)rx);
        write_be32(header + 17, (uint32_t)rz);
        if (std::fwrite(header, 1, kHeaderSize, fp_) != kHeaderSize) {
            std::fclose(fp_); fp_ = nullptr;
            return false;
        }
        std::vector<uint8_t> zero_table(kTableSize, 0);
        if (std::fwrite(zero_table.data(), 1, kTableSize, fp_) != kTableSize) {
            std::fclose(fp_); fp_ = nullptr;
            return false;
        }
        std::fflush(fp_);
        table_.fill(0);
        next_free_sector_ = 1;
    } else {
        if (!readHeaderAndTable()) {
            std::fclose(fp_); fp_ = nullptr;
            return false;
        }
    }
    return true;
}

void AtmkRegionFile::close() {
    if (fp_) {
        std::fflush(fp_);
        std::fclose(fp_);
        fp_ = nullptr;
    }
}

bool AtmkRegionFile::readHeaderAndTable() {
    if (!fp_) return false;
    std::fseek(fp_, 0, SEEK_SET);
    uint8_t header[kHeaderSize];
    if (std::fread(header, 1, kHeaderSize, fp_) != kHeaderSize) return false;
    if (!(header[0] == 'A' && header[1] == 'T' &&
          header[2] == 'M' && header[3] == 'K')) {
        return false;
    }
    region_x_ = (int32_t)read_be32(header + 13);
    region_z_ = (int32_t)read_be32(header + 17);

    uint8_t table_buf[kTableSize];
    if (std::fread(table_buf, 1, kTableSize, fp_) != kTableSize) return false;
    for (size_t i = 0; i < kTableEntries; ++i) {
        table_[i] = read_be32(table_buf + i * 4);
    }

    next_free_sector_ = 1;
    for (size_t i = 0; i < kTableEntries; ++i) {
        uint32_t entry = table_[i];
        if (entry == 0) continue;
        uint32_t off = entry & 0x00FFFFFF;
        uint8_t  cnt = (uint8_t)((entry >> 24) & 0xFF);
        uint32_t end = off + cnt;
        if (end > next_free_sector_) next_free_sector_ = end;
    }
    return true;
}

bool AtmkRegionFile::writeTable() {
    if (!fp_) return false;
    std::fseek(fp_, (long)kHeaderSize, SEEK_SET);
    uint8_t table_buf[kTableSize];
    for (size_t i = 0; i < kTableEntries; ++i) {
        write_be32(table_buf + i * 4, table_[i]);
    }
    if (std::fwrite(table_buf, 1, kTableSize, fp_) != kTableSize) return false;
    std::fflush(fp_);
    return true;
}

uint32_t AtmkRegionFile::allocSectors(uint8_t sectorCount) {
    uint32_t off = next_free_sector_;
    next_free_sector_ += sectorCount;
    std::fseek(fp_, 0, SEEK_END);
    long cur = std::ftell(fp_);
    long need_end = (long)next_free_sector_ * (long)kSectorSize;
    if (cur < need_end) {
        std::vector<uint8_t> pad(need_end - cur, 0);
        std::fwrite(pad.data(), 1, pad.size(), fp_);
    }
    return off;
}

std::optional<std::vector<uint8_t>>
AtmkRegionFile::readChunk(int32_t localCx, int32_t localCz) {
    if (!fp_) return std::nullopt;
    size_t idx = tableIndex(localCx, localCz);
    uint32_t entry = table_[idx];
    if (entry == 0) return std::nullopt;
    uint32_t off = entry & 0x00FFFFFF;
    uint8_t  cnt = (uint8_t)((entry >> 24) & 0xFF);
    if (off == 0 || cnt == 0) return std::nullopt;

    long byte_off = (long)off * (long)kSectorSize;
    if (std::fseek(fp_, byte_off, SEEK_SET) != 0) return std::nullopt;

    uint8_t hdr[5];
    if (std::fread(hdr, 1, 5, fp_) != 5) return std::nullopt;
    uint32_t comp_len = read_be32(hdr);
    uint8_t  comp_type = hdr[4];
    if (comp_len == 0 || comp_len > kMaxPayloadSize) return std::nullopt;

    std::vector<uint8_t> comp_buf(comp_len);
    if (std::fread(comp_buf.data(), 1, comp_len, fp_) != comp_len)
        return std::nullopt;

    std::vector<uint8_t> out;
    if (comp_type == 0) {
        out = comp_buf;
    } else if (comp_type == 1) {
        uLongf out_len = compressBound(comp_len) * 4 + 65536;
        out.resize(out_len);
        int rc = uncompress(out.data(), &out_len, comp_buf.data(),
                              (uLong)comp_buf.size());
        if (rc != Z_OK) {
            out_len = comp_len * 8 + 65536;
            out.resize(out_len);
            rc = uncompress(out.data(), &out_len, comp_buf.data(),
                              (uLong)comp_buf.size());
            if (rc != Z_OK) return std::nullopt;
        }
        out.resize(out_len);
    } else {
        return std::nullopt;
    }
    return out;
}

bool AtmkRegionFile::writeChunk(int32_t localCx, int32_t localCz,
                                  uint8_t compressionType,
                                  const std::vector<uint8_t>& compressedPayload) {
    if (!fp_ || !writable_) return false;
    if (compressedPayload.size() > kMaxPayloadSize) return false;

    size_t needed = 5 + compressedPayload.size();
    size_t sector_count_sz = (needed + kSectorSize - 1) / kSectorSize;
    if (sector_count_sz == 0 || sector_count_sz > 255) return false;
    uint8_t sector_count = (uint8_t)sector_count_sz;

    uint32_t off = allocSectors(sector_count);
    long byte_off = (long)off * (long)kSectorSize;
    if (std::fseek(fp_, byte_off, SEEK_SET) != 0) return false;

    uint8_t hdr[5];
    write_be32(hdr + 0, (uint32_t)compressedPayload.size());
    hdr[4] = compressionType;
    if (std::fwrite(hdr, 1, 5, fp_) != 5) return false;
    if (!compressedPayload.empty()) {
        if (std::fwrite(compressedPayload.data(), 1,
                          compressedPayload.size(), fp_)
              != compressedPayload.size()) return false;
    }
    size_t written = 5 + compressedPayload.size();
    size_t total   = (size_t)sector_count * kSectorSize;
    if (written < total) {
        std::vector<uint8_t> pad(total - written, 0);
        if (std::fwrite(pad.data(), 1, pad.size(), fp_) != pad.size())
            return false;
    }
    std::fflush(fp_);

    size_t idx = tableIndex(localCx, localCz);
    table_[idx] = (off & 0x00FFFFFF) | ((uint32_t)sector_count << 24);
    return writeTable();
}

bool AtmkRegionFile::hasChunk(int32_t localCx, int32_t localCz) const {
    return table_[tableIndex(localCx, localCz)] != 0;
}

std::vector<std::pair<int32_t, int32_t>>
AtmkRegionFile::listChunks() const {
    std::vector<std::pair<int32_t, int32_t>> out;
    for (int32_t lz = 0; lz < 32; ++lz) {
        for (int32_t lx = 0; lx < 32; ++lx) {
            if (table_[tableIndex(lx, lz)] != 0) {
                out.emplace_back(region_x_ * 32 + lx,
                                   region_z_ * 32 + lz);
            }
        }
    }
    return out;
}

} // namespace atomikpe::level::format::atmkma
