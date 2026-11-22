/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

 * CurseStone | Open Source Server Software
 *
 * Copyright (C) 2026 CurseStoneORG
 *
 * Developed by CurseStoneORG.
 * Built for an open and extensible Minecraft server ecosystem.
 *
 * Licensed under the GNU Lesser General Public License v3.0 or later.
 * You may study, modify, and redistribute this software under its license.
 *
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 * Repository: https://github.com/CurseStoneORG/CurseStone
 *
 */
// SPDX-License-Identifier: LGPL-3.0-or-later
// DBFormat — the byte-level primitives of the real LevelDB on-disk format.
//
// The LevelDB world provider must read and write the SAME files the real
// MCPE 0.15.x client (and PocketMine) produce, so this is not a private
// container: every constant, record layout and CRC discipline here is the
// one Google's LevelDB (and the MCPE fork of it) defines:
//
//   Internal key   user key + 8-byte trailer, LE: sequence(56) << 8 | type(8)
//   Log/manifest   32 KiB blocks; record = CRC32C(4) + len(2) + type(1)
//                  + payload, types FULL/FIRST/MIDDLE/LAST (7/2/3/4, +1
//                  for masked-CRC-recycled records)
//   VersionEdit    varint32 tag stream (comparator, log number, next file,
//                  last sequence, deleted file, new file ...)
//   Table block    entries: shared(Var32) + non_shared(Var32) + value_len
//                  (Var32) + key delta + value; restart points every 16
//                  entries; 5-byte trailer (CRC32C + compression byte)
//   Block types    0 = raw, 1 = snappy, 2 = zlib (the MCPE fork addition
//                  PocketMine also writes)
//   Footer         metaindex handle + index handle, padded to 4096 total,
//                  8-byte magic 0xdb4775248b80fb57 LE
//
// Reads accept raw/snappy/zlib blocks so worlds written by the real
// device (snappy) and by PocketMine (zlib) both open; writes emit zlib
// level 1 (measured on Anvil's chunk payloads: ~3x faster than the
// default level for +14% size — the same trade a live server wants).
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace cursestone::world::format::leveldb::db {

// ---- constants shared with the on-disk format --------------------------

constexpr uint64_t kTableMagic = 0xdb4775248b80fb57ull;
constexpr size_t kLogBlockSize = 32768;
constexpr size_t kFooterSize = 48;
constexpr size_t kBlockSize = 4096;
constexpr int kRestartInterval = 16;
constexpr uint8_t kTypeValue = 1;
constexpr uint8_t kTypeDeletion = 0;
constexpr uint8_t kCompressionNone = 0;
constexpr uint8_t kCompressionSnappy = 1;
constexpr uint8_t kCompressionZlib = 2;
constexpr uint64_t kMaxSequenceNumber = (1ull << 56) - 1;
// Ceiling for any decompressed table block. A real block is at most a few
// MiB; anything asking for more is corruption, not data.
constexpr size_t kMaxBlockInflate = 32u << 20; // 32 MiB
constexpr const char* kComparatorName = "leveldb.BytewiseComparator";

// VersionEdit tags (the real leveldb numbering).
enum VersionEditTag : uint32_t {
    kTagComparator = 1,
    kTagLogNumber = 2,
    kTagNextFileNumber = 3,
    kTagLastSequence = 4,
    kTagCompactPointer = 5,
    kTagDeletedFile = 6,
    kTagNewFile = 7,
    kTagPrevLogNumber = 9,
};

// One live table file as the manifest describes it.
struct FileMeta {
    int level = 0;
    uint64_t number = 0;
    uint64_t size = 0;
    std::string smallest; // internal key
    std::string largest;  // internal key
};

// ---- varints -------------------------------------------------------------

inline void put_var_u32(std::vector<uint8_t>* out, uint32_t v) {
    while (v >= 0x80) {
        out->push_back((uint8_t)(v | 0x80));
        v >>= 7;
    }
    out->push_back((uint8_t)v);
}

inline void put_var_u64(std::vector<uint8_t>* out, uint64_t v) {
    while (v >= 0x80) {
        out->push_back((uint8_t)(v | 0x80));
        v >>= 7;
    }
    out->push_back((uint8_t)v);
}

// Returns false when the buffer ends inside the varint. `pos` advances only
// on success.
bool get_var_u32(const uint8_t* data, size_t len, size_t* pos, uint32_t* out);
bool get_var_u64(const uint8_t* data, size_t len, size_t* pos, uint64_t* out);

// ---- CRC32C (Castagnoli), masked the way leveldb log records do ----------

uint32_t crc32c(const uint8_t* data, size_t len, uint32_t seed = 0);
inline uint32_t mask_crc(uint32_t crc) {
    return ((crc >> 15) | (crc << 17)) + 0xa282ead8u;
}
inline uint32_t unmask_crc(uint32_t masked) {
    uint32_t rot = masked - 0xa282ead8u;
    return (rot >> 17) | (rot << 15);
}

// ---- snappy ---------------------------------------------------------------

// Full snappy decoder (literal + copy[1|2|4] tags). Device worlds store
// table blocks snappy-compressed; reading them without the real library
// needs this. Returns false on malformed input; *out grows as needed.
bool snappy_decompress(const uint8_t* in, size_t in_len, std::vector<uint8_t>* out);

// ---- log/manifest records --------------------------------------------------

enum LogRecordType : uint8_t {
    kLogFull = 1,
    kLogFirst = 2,
    kLogMiddle = 3,
    kLogLast = 4,
};

// Append one logical record (split across 32 KiB blocks when needed).
// `start_offset` is the byte position the record will begin at inside the
// file (0 for a fresh file); block-boundary math uses it so a WAL that
// already holds earlier records stays aligned.
void log_append_record(std::vector<uint8_t>* file_bytes, const uint8_t* payload,
                       size_t len, uint64_t start_offset = 0);

// Reader over a whole log/manifest image. `next()` yields each logical
// record; torn/unknown-type records end the stream (leveldb treats a
// damaged tail as EOF).
class LogReader {
  public:
    explicit LogReader(const std::vector<uint8_t>& image) : image_(image) {}
    // Payload is only valid until the next next() call.
    bool next(const uint8_t** payload, size_t* len);

  private:
    const std::vector<uint8_t>& image_;
    size_t pos_ = 0;
};

// ---- internal keys ----------------------------------------------------------

// Build the 8-byte trailer (sequence<<8|type, LE) and the full internal key.
inline std::string make_internal_key(const std::string& user_key, uint64_t seq,
                                     uint8_t type) {
    std::string out = user_key;
    uint64_t trailer = (seq << 8) | type;
    for (int i = 0; i < 8; ++i)
        out.push_back((uint8_t)(trailer >> (8 * i)));
    return out;
}

inline std::string user_key_of(const std::string& internal) {
    return internal.size() >= 8 ? internal.substr(0, internal.size() - 8) : internal;
}

// The 8-byte trailer is little-endian: byte 0 = type, bytes 1..7 = sequence.
inline uint64_t sequence_of(const std::string& internal) {
    if (internal.size() < 8)
        return 0;
    uint64_t t = 0;
    for (int i = 0; i < 7; ++i)
        t |= (uint64_t)(uint8_t)internal[internal.size() - 7 + i] << (8 * i);
    return t;
}

inline uint8_t type_of(const std::string& internal) {
    return internal.size() >= 8 ? (uint8_t)internal[internal.size() - 8] : 0;
}

// Bytewise user-key order, then NEWEST first (higher sequence, then higher
// type). This is the ordering every table and the memtable sort by.
bool internal_key_less(const std::string& a, const std::string& b);

// leveldb's FindShortestSeparator on internal keys: shrink the user part by
// one byte when possible and force the seek-max trailer so index entries
// stay small and Get()s land on the right block.
std::string find_shortest_separator(const std::string& start, const std::string& limit);
// FindShortSuccessor: smallest internal key strictly greater than `key`'s
// user part (used for the index handle after the last block).
std::string find_short_successor(const std::string& key);

// ---- VersionEdit --------------------------------------------------------------

struct VersionEdit {
    std::string comparator;
    uint64_t log_number = 0;
    uint64_t prev_log_number = 0;
    uint64_t next_file_number = 0;
    uint64_t last_sequence = 0;
    std::vector<uint64_t> deleted_files; // (level<<32|number)
    std::vector<FileMeta> added_files;

    void encode(std::vector<uint8_t>* out) const;
    // Decodes one edit; returns false on malformed input.
    bool decode(const uint8_t* data, size_t len);
};

} // namespace cursestone::world::format::leveldb::db
