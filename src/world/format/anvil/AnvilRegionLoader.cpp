/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

 * CurseStone | Open Source Server Software
 *
 * Copyright (C) 2026 CurseStoneTeam
 *
 * Developed by CurseStoneTeam.
 * Built for an open and extensible Minecraft server ecosystem.
 *
 * Licensed under the GNU Lesser General Public License v3.0 or later.
 * You may study, modify, and redistribute this software under its license.
 *
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 * Repository: https://github.com/CurseStoneTeam/CurseStone
 *
 */
// SPDX-License-Identifier: LGPL-3.0-or-later
// AnvilRegionLoader.cpp — single .mca region file implementation.
//
// Mirrors Nukkit's RegionLoader + BaseRegionLoader logic:
//   - open() / createBlank() / loadLocationTable()
//   - readChunk() reads [int length][byte compression][payload]
//   - writeChunk() re-allocates sectors at end-of-file when the chunk
//     grows, pads to a multiple of 4096, and rewrites the location
//     table entry.
#include "AnvilRegionLoader.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include "../../../utils/PlatformCompat.h" // ftruncate / fileno
#include <zlib.h>

namespace cursestone::world::format::anvil {

static void write_be32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)(v);
}
static uint32_t read_be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static long now_unix_seconds() {
    return (long)std::time(nullptr);
}

AnvilRegionLoader::AnvilRegionLoader() = default;
AnvilRegionLoader::~AnvilRegionLoader() {
    close();
}

bool AnvilRegionLoader::open(const std::string& path, int32_t rx, int32_t rz) {
    path_ = path;
    region_x_ = rx;
    region_z_ = rz;

    // Try to open existing file r+b; fall back to w+b for new file.
    fp_ = std::fopen(path.c_str(), "r+b");
    if (!fp_) {
        fp_ = std::fopen(path.c_str(), "w+b");
        if (!fp_)
            return false;
    }
    std::fseek(fp_, 0, SEEK_END);
    long sz = std::ftell(fp_);
    if (sz == 0) {
        // Brand-new file — write blank 8 KiB header.
        if (!createBlank()) {
            std::fclose(fp_);
            fp_ = nullptr;
            return false;
        }
    } else {
        if (!loadLocationTable()) {
            std::fclose(fp_);
            fp_ = nullptr;
            return false;
        }
    }
    last_used_ = std::chrono::duration_cast<std::chrono::milliseconds>(
                     std::chrono::system_clock::now().time_since_epoch())
                     .count();
    return true;
}

void AnvilRegionLoader::close() {
    if (fp_) {
        std::fflush(fp_);
        std::fclose(fp_);
        fp_ = nullptr;
    }
}

bool AnvilRegionLoader::createBlank() {
    if (!fp_)
        return false;
    std::fseek(fp_, 0, SEEK_SET);
    ::ftruncate(fileno(fp_), 0);
    last_sector_ = 1;
    uint32_t now = (uint32_t)now_unix_seconds();

    // Location table: 1024 × (offset=0 | count=0) = 4096 zero bytes.
    uint8_t loc_buf[kLocationTableSize];
    std::memset(loc_buf, 0, sizeof(loc_buf));
    if (std::fwrite(loc_buf, 1, kLocationTableSize, fp_) != kLocationTableSize)
        return false;

    // Timestamp table: 1024 × now.
    uint8_t ts_buf[kTimestampTableSize];
    for (size_t i = 0; i < kChunkSlotCount; ++i) {
        write_be32(ts_buf + i * 4, now);
        table_[i] = LocationEntry{0, 0, now};
    }
    if (std::fwrite(ts_buf, 1, kTimestampTableSize, fp_) != kTimestampTableSize)
        return false;

    std::fflush(fp_);
    return true;
}

bool AnvilRegionLoader::loadLocationTable() {
    if (!fp_)
        return false;
    std::fseek(fp_, 0, SEEK_SET);

    uint8_t loc_buf[kLocationTableSize];
    if (std::fread(loc_buf, 1, kLocationTableSize, fp_) != kLocationTableSize)
        return false;
    uint8_t ts_buf[kTimestampTableSize];
    if (std::fread(ts_buf, 1, kTimestampTableSize, fp_) != kTimestampTableSize)
        return false;

    // Nukkit semantics: lastSector = the highest sector index in use.
    // Initial value is 1 (sector 0 + 1 are the header / location table).
    last_sector_ = 1;
    for (size_t i = 0; i < kChunkSlotCount; ++i) {
        uint32_t raw = read_be32(loc_buf + i * 4);
        uint32_t off = (raw >> 8) & 0x00FFFFFF;
        uint8_t cnt = (uint8_t)(raw & 0xFF);
        uint32_t ts = read_be32(ts_buf + i * 4);
        table_[i] = LocationEntry{off, cnt, ts};
        if (off != 0 && cnt != 0) {
            // Last sector used by this chunk = off + cnt - 1.
            uint32_t last = off + cnt - 1;
            if (last > last_sector_)
                last_sector_ = last;
        }
    }
    return true;
}

bool AnvilRegionLoader::writeLocationTable() {
    if (!fp_)
        return false;
    std::fseek(fp_, 0, SEEK_SET);
    uint8_t loc_buf[kLocationTableSize];
    for (size_t i = 0; i < kChunkSlotCount; ++i) {
        uint32_t raw = ((table_[i].offset & 0x00FFFFFF) << 8) | (table_[i].count & 0xFF);
        write_be32(loc_buf + i * 4, raw);
    }
    if (std::fwrite(loc_buf, 1, kLocationTableSize, fp_) != kLocationTableSize)
        return false;

    uint8_t ts_buf[kTimestampTableSize];
    for (size_t i = 0; i < kChunkSlotCount; ++i) {
        write_be32(ts_buf + i * 4, table_[i].timestamp);
    }
    if (std::fwrite(ts_buf, 1, kTimestampTableSize, fp_) != kTimestampTableSize)
        return false;
    std::fflush(fp_);
    return true;
}

bool AnvilRegionLoader::writeLocationIndex(size_t index) {
    if (!fp_)
        return false;
    // Location entry at offset index*4 within [0..4095].
    std::fseek(fp_, (long)(index * 4), SEEK_SET);
    uint8_t buf[4];
    uint32_t raw = ((table_[index].offset & 0x00FFFFFF) << 8) | (table_[index].count & 0xFF);
    write_be32(buf, raw);
    if (std::fwrite(buf, 1, 4, fp_) != 4)
        return false;
    // Timestamp entry at offset 4096 + index*4.
    std::fseek(fp_, (long)(kLocationTableSize + index * 4), SEEK_SET);
    write_be32(buf, table_[index].timestamp);
    if (std::fwrite(buf, 1, 4, fp_) != 4)
        return false;
    std::fflush(fp_);
    return true;
}

std::optional<std::vector<uint8_t>> AnvilRegionLoader::readChunk(int32_t lx, int32_t lz) {
    if (!fp_)
        return std::nullopt;
    size_t idx = slotIndex(lx, lz);
    const LocationEntry& e = table_[idx];
    if (e.offset == 0 || e.count == 0)
        return std::nullopt;
    last_used_ = std::chrono::duration_cast<std::chrono::milliseconds>(
                     std::chrono::system_clock::now().time_since_epoch())
                     .count();

    long byte_off = (long)e.offset * (long)kSectorSize;
    if (std::fseek(fp_, byte_off, SEEK_SET) != 0)
        return std::nullopt;

    uint8_t hdr[5];
    if (std::fread(hdr, 1, 5, fp_) != 5)
        return std::nullopt;
    uint32_t length = read_be32(hdr);
    uint8_t compression = hdr[4];

    // length includes the 1-byte compression prefix; payload is length-1.
    if (length == 0)
        return std::nullopt;
    if (length >= (kMaxChunkSectors * kSectorSize))
        return std::nullopt;
    if (compression != kCompressionGzip && compression != kCompressionZlib)
        return std::nullopt;

    size_t payload_len = (length > 1) ? (size_t)(length - 1) : 0;
    std::vector<uint8_t> payload(payload_len);
    if (payload_len > 0) {
        if (std::fread(payload.data(), 1, payload_len, fp_) != payload_len)
            return std::nullopt;
    }
    return payload;
}

bool AnvilRegionLoader::writeChunk(int32_t lx, int32_t lz, uint8_t compression,
                                   const std::vector<uint8_t>& payload) {
    if (!fp_)
        return false;
    if (compression != kCompressionGzip && compression != kCompressionZlib)
        return false;

    // length = payload + 1 (compression byte).
    uint32_t length = (uint32_t)(payload.size() + 1);
    if (length + 4 > (kMaxChunkSectors * (uint32_t)kSectorSize))
        return false;

    // Number of sectors needed for [length(4) | compression(1) | payload].
    size_t total_bytes = 4 + 1 + payload.size();
    uint32_t sectors = (uint32_t)((total_bytes + kSectorSize - 1) / kSectorSize);
    if (sectors == 0)
        sectors = 1;
    if (sectors > 255)
        return false;

    size_t idx = slotIndex(lx, lz);
    LocationEntry& e = table_[idx];
    bool index_changed = false;
    if (e.count < sectors) {
        // Doesn't fit in current allocation → append at end of file.
        // Mirror Nukkit: table[0] = lastSector + 1, then lastSector += sectors.
        e.offset = last_sector_ + 1;
        last_sector_ += sectors;
        index_changed = true;
    } else if (e.count != sectors) {
        index_changed = true;
    }
    e.count = (uint8_t)sectors;
    e.timestamp = (uint32_t)now_unix_seconds();

    long byte_off = (long)e.offset * (long)kSectorSize;
    // Ensure the file is large enough for the seek target. When writing to
    // a freshly-created region (only 8 KiB of header) this extends the file
    // past its current EOF so fseek+write don't fail or fragment.
    std::fseek(fp_, 0, SEEK_END);
    long cur_end = std::ftell(fp_);
    long needed_end = byte_off + (long)sectors * (long)kSectorSize;
    if (cur_end < needed_end) {
        long pad = needed_end - cur_end;
        std::vector<uint8_t> zero(pad, 0);
        std::fseek(fp_, cur_end, SEEK_SET);
        if (std::fwrite(zero.data(), 1, zero.size(), fp_) != zero.size())
            return false;
    }
    if (std::fseek(fp_, byte_off, SEEK_SET) != 0)
        return false;

    // Write [length BE][compression][payload], then zero-pad to sector multiple.
    uint8_t len_buf[4];
    write_be32(len_buf, length);
    if (std::fwrite(len_buf, 1, 4, fp_) != 4)
        return false;
    if (std::fwrite(&compression, 1, 1, fp_) != 1)
        return false;
    if (!payload.empty()) {
        if (std::fwrite(payload.data(), 1, payload.size(), fp_) != payload.size())
            return false;
    }
    // Pad to sectors * kSectorSize.
    size_t written = 4 + 1 + payload.size();
    size_t padded = (size_t)sectors * kSectorSize;
    if (written < padded) {
        std::vector<uint8_t> zero(padded - written, 0);
        if (std::fwrite(zero.data(), 1, zero.size(), fp_) != zero.size())
            return false;
    }
    std::fflush(fp_);
    if (index_changed) {
        if (!writeLocationIndex(idx))
            return false;
    }
    last_used_ = std::chrono::duration_cast<std::chrono::milliseconds>(
                     std::chrono::system_clock::now().time_since_epoch())
                     .count();
    return true;
}

bool AnvilRegionLoader::hasChunk(int32_t lx, int32_t lz) const {
    size_t idx = slotIndex(lx, lz);
    return table_[idx].offset != 0 && table_[idx].count != 0;
}

} // namespace cursestone::world::format::anvil
