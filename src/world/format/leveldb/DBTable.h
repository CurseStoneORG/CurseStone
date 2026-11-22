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
// DBTable — the .ldb SSTable codec: builds and reads the real LevelDB table
// format (data blocks with restart points, block trailer + compression byte,
// index block, metaindex, 4096-byte footer with the leveldb magic).
//
// Reads tolerate every block compression the target ecosystem writes:
// raw (0), snappy (1, real MCPE device worlds) and zlib (2, PocketMine).
// Writes emit zlib level-1 blocks — valid for every reader of the format
// and the best speed/size trade for a live server (see Anvil's notes on
// compression level).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "DBFormat.h"

namespace cursestone::world::format::leveldb::db {

// One (internal key, value) row. value.empty() + type deletion encodes a
// tombstone via the trailer type instead of a payload type flag.
struct TableEntry {
    std::string key;    // internal key (user key + 8-byte trailer)
    std::string value;  // payload bytes ("" for tombstones)
};

// Encode one table from SORTED entries (internal_key_less order) into a
// complete .ldb image. Returns false on internal error (allocation/deflate).
bool build_table(const std::vector<TableEntry>& entries, std::vector<uint8_t>* out);

// Parsed view of one data block: entries in key order.
struct ParsedBlock {
    std::vector<TableEntry> entries;
};

// Reader over a finished table image. The image stays owned by the caller
// and must outlive the reader. The index is parsed once; data blocks are
// decompressed on demand and never cached (the OS page cache handles it).
class TableReader {
  public:
    // Parses the footer + index. Returns nullptr-class (ok()==false) on
    // corruption.
    bool open(const uint8_t* image, size_t len);

    // Number of entries claimed by the index (upper bound, informational).
    size_t block_count() const { return index_.size(); }

    // Point lookup by internal key. Returns the newest entry for the user
    // key of `internal` (the caller passes the target internal key; the
    // search matches user keys per leveldb semantics). Sets *found.
    bool get(const std::string& internal, TableEntry* out) const;

    // Full scan in key order (used by compaction and listChunks).
    void scan(std::vector<TableEntry>* out) const;

    // Boundaries as recorded in the index.
    const std::string& first_key_hint() const { return index_.front().separator; }
    const std::string& last_key_hint() const { return last_key_; }

  private:
    // BlockHandle: varint64 offset + varint64 size.
    struct IndexEntry {
        std::string separator; // ≥ all keys in the block
        uint64_t offset = 0;
        uint64_t size = 0;
    };

    static bool read_handle(const uint8_t* data, size_t len, size_t* pos,
                            uint64_t* offset, uint64_t* size);
    // Parses one (possibly compressed) block body into entries.
    static bool parse_block(const uint8_t* data, size_t len, ParsedBlock* out);
    // Fetches + decompresses one block by handle; also verifies its CRC
    // trailer (last 5 bytes: crc32c(4) + compression type(1), CRC over the
    // uncompressed bytes).
    bool read_block(uint64_t offset, uint64_t size, ParsedBlock* out) const;

    const uint8_t* image_ = nullptr;
    size_t image_len_ = 0;
    std::vector<IndexEntry> index_;
    std::string last_key_; // largest internal key in the table
};

} // namespace cursestone::world::format::leveldb::db
