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
// DBTable.cpp — SSTable build/read implementation.
#include "DBTable.h"

#include <algorithm>
#include <cstring>
#include <zlib.h>

namespace cursestone::world::format::leveldb::db {
namespace {

// leveldb's rule: a compressed block only wins if it saves at least 12.5%.
bool compression_wins(size_t raw, size_t compressed) {
    return compressed + (compressed / 8) < raw;
}

bool zlib_compress_block(const uint8_t* src, size_t src_len,
                         std::vector<uint8_t>* out) {
    uLongf bound = compressBound((uLong)src_len);
    out->resize(bound);
    uLongf dest_len = bound;
    if (compress2(out->data(), &dest_len, src, (uLong)src_len, 1) != Z_OK) {
        out->clear();
        return false;
    }
    out->resize(dest_len);
    return true;
}

bool zlib_decompress_block(const uint8_t* src, size_t src_len, size_t max_out,
                           std::vector<uint8_t>* out) {
    // `max_out` is a corruption guard, not the exact size: the block's real
    // length is whatever zlib produces. Start from a heuristic and grow on
    // Z_BUF_ERROR until the cap.
    size_t guess = src_len * 4;
    if (guess < 1024)
        guess = 1024;
    if (guess > max_out)
        guess = max_out;
    for (;;) {
        out->resize(guess);
        uLongf dest_len = guess;
        int rc = uncompress(out->data(), &dest_len, src, (uLong)src_len);
        if (rc == Z_BUF_ERROR && guess < max_out) {
            guess *= 2;
            if (guess > max_out)
                guess = max_out;
            continue;
        }
        if (rc != Z_OK) {
            out->clear();
            return false;
        }
        out->resize(dest_len);
        return true;
    }
}

// ---- block builder (shared by data/index/metaindex) -----------------------

class BlockBuilder {
  public:
    explicit BlockBuilder(int restart_interval) : restart_interval_(restart_interval) {
        restarts_.push_back(0);
    }

    void add(const std::string& key, const std::string& value) {
        // Restart entry: no prefix sharing, record the offset.
        if (counter_ == restart_interval_) {
            restarts_.push_back((uint32_t)body_.size());
            counter_ = 0;
        }
        size_t shared = 0;
        if (counter_ > 0 && !key.empty()) {
            const std::string& last = last_key_;
            size_t common = (last.size() < key.size()) ? last.size() : key.size();
            while (shared < common && last[shared] == key[shared])
                ++shared;
        }
        put_var_u32(&body_, (uint32_t)shared);
        put_var_u32(&body_, (uint32_t)(key.size() - shared));
        put_var_u32(&body_, (uint32_t)value.size());
        body_.insert(body_.end(), key.begin() + shared, key.end());
        body_.insert(body_.end(), value.begin(), value.end());
        last_key_ = key;
        ++counter_;
        ++entries_;
    }

    // Finish: restarts array + count. Empty blocks still carry one restart
    // (offset 0) + count 1, exactly like leveldb's empty metaindex.
    std::vector<uint8_t> finish() {
        std::vector<uint8_t> out = std::move(body_);
        body_.clear();
        body_.shrink_to_fit();
        for (uint32_t r : restarts_) {
            out.push_back((uint8_t)r);
            out.push_back((uint8_t)(r >> 8));
            out.push_back((uint8_t)(r >> 16));
            out.push_back((uint8_t)(r >> 24));
        }
        uint32_t n = (uint32_t)restarts_.size();
        out.push_back((uint8_t)n);
        out.push_back((uint8_t)(n >> 8));
        out.push_back((uint8_t)(n >> 16));
        out.push_back((uint8_t)(n >> 24));
        restarts_.clear();
        restarts_.push_back(0);
        counter_ = 0;
        entries_ = 0;
        last_key_.clear();
        return out;
    }

    size_t estimated_size() const { return body_.size() + restarts_.size() * 4 + 4; }

  private:
    int restart_interval_;
    int counter_ = 0;   // entries since last restart
    size_t entries_ = 0; // total entries (prefix sharing guard)
    std::vector<uint32_t> restarts_;
    std::string last_key_;
    std::vector<uint8_t> body_;
};

} // namespace

// ---------------------------------------------------------------------------
// build_table
// ---------------------------------------------------------------------------

bool build_table(const std::vector<TableEntry>& entries, std::vector<uint8_t>* out) {
    out->clear();
    if (entries.empty())
        return false;

    // Data blocks. Each block records its boundary keys so the index
    // separators can be fixed afterwards (separator_i = shortest key ≥
    // last_key_i and < first_key_{i+1} — the leveldb invariant).
    struct BlockMeta {
        std::string first_key;
        std::string last_key;
        uint64_t offset;
        uint64_t size;
    };
    std::vector<BlockMeta> blocks;
    BlockBuilder block(kRestartInterval);
    size_t block_raw_size = 0;
    std::string block_first_key, block_last_key;

    auto flush_block = [&]() {
        if (block_raw_size == 0)
            return;
        std::vector<uint8_t> raw = block.finish();
        // Choose compression: zlib level 1 when it wins the 12.5% rule.
        std::vector<uint8_t> compressed;
        uint8_t type = kCompressionNone;
        const uint8_t* stored = raw.data();
        size_t stored_len = raw.size();
        if (zlib_compress_block(raw.data(), raw.size(), &compressed) &&
            compression_wins(raw.size(), compressed.size())) {
            type = kCompressionZlib;
            stored = compressed.data();
            stored_len = compressed.size();
        }
        uint64_t offset = out->size();
        out->insert(out->end(), stored, stored + stored_len);
        uint32_t crc = mask_crc(crc32c(stored, stored_len));
        out->push_back((uint8_t)crc);
        out->push_back((uint8_t)(crc >> 8));
        out->push_back((uint8_t)(crc >> 16));
        out->push_back((uint8_t)(crc >> 24));
        out->push_back(type);
        blocks.push_back({std::move(block_first_key), std::move(block_last_key),
                          offset, (uint64_t)stored_len});
        block_first_key.clear();
        block_last_key.clear();
        block_raw_size = 0;
    };

    for (const TableEntry& e : entries) {
        if (block_raw_size == 0)
            block_first_key = e.key;
        if (block_raw_size >= kBlockSize) {
            flush_block();
            block_first_key = e.key;
        }
        block_last_key = e.key;
        block.add(e.key, e.value);
        block_raw_size += e.key.size() + e.value.size() + 5;
    }
    flush_block();

    if (blocks.empty())
        return false;

    // Index block: separator → BlockHandle (varint offset + varint size).
    std::vector<std::string> separators;
    separators.reserve(blocks.size());
    for (size_t i = 0; i < blocks.size(); ++i) {
        if (i + 1 < blocks.size())
            separators.push_back(find_shortest_separator(blocks[i].last_key,
                                                         blocks[i + 1].first_key));
        else
            separators.push_back(find_short_successor(blocks[i].last_key));
    }
    BlockBuilder index_block(kRestartInterval);
    std::vector<uint8_t> handle;
    for (size_t i = 0; i < blocks.size(); ++i) {
        handle.clear();
        put_var_u64(&handle, blocks[i].offset);
        put_var_u64(&handle, blocks[i].size);
        index_block.add(separators[i],
                        std::string((const char*)handle.data(), handle.size()));
    }
    std::vector<uint8_t> index_raw = index_block.finish();
    uint64_t index_offset = out->size();
    uint8_t index_type = kCompressionNone;
    std::vector<uint8_t> index_compressed;
    const uint8_t* index_stored = index_raw.data();
    size_t index_stored_len = index_raw.size();
    if (zlib_compress_block(index_raw.data(), index_raw.size(), &index_compressed) &&
        compression_wins(index_raw.size(), index_compressed.size())) {
        index_type = kCompressionZlib;
        index_stored = index_compressed.data();
        index_stored_len = index_compressed.size();
    }
    out->insert(out->end(), index_stored, index_stored + index_stored_len);
    {
        uint32_t crc = mask_crc(crc32c(index_stored, index_stored_len));
        out->push_back((uint8_t)crc);
        out->push_back((uint8_t)(crc >> 8));
        out->push_back((uint8_t)(crc >> 16));
        out->push_back((uint8_t)(crc >> 24));
        out->push_back(index_type);
    }
    uint64_t index_size = out->size() - index_offset - 5;

    // Metaindex: empty (no filter policy), stored raw.
    BlockBuilder metaindex(kRestartInterval);
    std::vector<uint8_t> meta_raw = metaindex.finish();
    uint64_t meta_offset = out->size();
    out->insert(out->end(), meta_raw.begin(), meta_raw.end());
    {
        uint32_t crc = mask_crc(crc32c(meta_raw.data(), meta_raw.size()));
        out->push_back((uint8_t)crc);
        out->push_back((uint8_t)(crc >> 8));
        out->push_back((uint8_t)(crc >> 16));
        out->push_back((uint8_t)(crc >> 24));
        out->push_back(kCompressionNone);
    }
    uint64_t meta_size = out->size() - meta_offset - 5;

    // Footer: metaindex handle + index handle, zero padding to 4096 total,
    // then the 8-byte magic LE.
    std::vector<uint8_t> footer;
    put_var_u64(&footer, meta_offset);
    put_var_u64(&footer, meta_size);
    put_var_u64(&footer, index_offset);
    put_var_u64(&footer, index_size);
    if (footer.size() > kFooterSize - 8)
        return false; // absurdly large handles: cannot fit
    footer.resize(kFooterSize - 8, 0);
    for (int i = 0; i < 8; ++i)
        footer.push_back((uint8_t)(kTableMagic >> (8 * i)));
    out->insert(out->end(), footer.begin(), footer.end());
    return true;
}

// ---------------------------------------------------------------------------
// TableReader
// ---------------------------------------------------------------------------

bool TableReader::read_handle(const uint8_t* data, size_t len, size_t* pos,
                              uint64_t* offset, uint64_t* size) {
    return get_var_u64(data, len, pos, offset) && get_var_u64(data, len, pos, size);
}

bool TableReader::parse_block(const uint8_t* data, size_t len, ParsedBlock* out) {
    out->entries.clear();
    if (len < 4)
        return false;
    uint32_t num_restarts = 0;
    {
        const uint8_t* p = data + len - 4;
        num_restarts = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
                       ((uint32_t)p[3] << 24);
    }
    if (num_restarts == 0 || (num_restarts + 1) * 4 > len)
        return false;
    size_t entries_end = len - (num_restarts + 1) * 4;
    size_t pos = 0;
    std::string key;
    while (pos < entries_end) {
        uint32_t shared = 0, non_shared = 0, value_len = 0;
        if (!get_var_u32(data, entries_end, &pos, &shared) ||
            !get_var_u32(data, entries_end, &pos, &non_shared) ||
            !get_var_u32(data, entries_end, &pos, &value_len))
            return false;
        if (shared > key.size() || non_shared > entries_end - pos ||
            value_len > entries_end - pos - non_shared)
            return false;
        key.resize(shared);
        key.append((const char*)data + pos, non_shared);
        pos += non_shared;
        TableEntry e;
        e.key = key;
        e.value.assign((const char*)data + pos, value_len);
        pos += value_len;
        out->entries.push_back(std::move(e));
    }
    return true;
}

bool TableReader::read_block(uint64_t offset, uint64_t size, ParsedBlock* out) const {
    if (offset + size + 5 > image_len_ || size > kMaxBlockInflate)
        return false;
    const uint8_t* body = image_ + offset;
    const uint8_t* trailer = body + size;
    uint32_t crc = (uint32_t)trailer[0] | ((uint32_t)trailer[1] << 8) |
                   ((uint32_t)trailer[2] << 16) | ((uint32_t)trailer[3] << 24);
    uint8_t type = trailer[4];
    if (mask_crc(crc32c(body, size)) != crc)
        return false;
    if (type == kCompressionNone)
        return parse_block(body, size, out);
    std::vector<uint8_t> inflated;
    if (type == kCompressionZlib) {
        if (!zlib_decompress_block(body, size, kMaxBlockInflate, &inflated))
            return false;
    } else if (type == kCompressionSnappy) {
        uint32_t expected = 0;
        size_t pos = 0;
        if (!get_var_u32(body, size, &pos, &expected) || expected > kMaxBlockInflate)
            return false;
        if (!snappy_decompress(body, size, &inflated) || inflated.size() != expected)
            return false;
    } else {
        return false; // unknown compression
    }
    return parse_block(inflated.data(), inflated.size(), out);
}

bool TableReader::open(const uint8_t* image, size_t len) {
    image_ = image;
    image_len_ = len;
    index_.clear();
    last_key_.clear();
    if (len < kFooterSize)
        return false;
    const uint8_t* footer = image + len - kFooterSize;
    uint64_t magic = 0;
    for (int i = 0; i < 8; ++i)
        magic |= (uint64_t)footer[kFooterSize - 8 + i] << (8 * i);
    if (magic != kTableMagic)
        return false;
    size_t pos = 0;
    uint64_t meta_offset = 0, meta_size = 0, index_offset = 0, index_size = 0;
    if (!read_handle(footer, kFooterSize, &pos, &meta_offset, &meta_size) ||
        !read_handle(footer, kFooterSize, &pos, &index_offset, &index_size))
        return false;
    if (index_size == 0 || index_offset + index_size + 5 > len)
        return false;
    (void)meta_offset;
    (void)meta_size;

    ParsedBlock index_block;
    if (!read_block(index_offset, index_size, &index_block))
        return false;
    for (const TableEntry& e : index_block.entries) {
        IndexEntry ie;
        ie.separator = e.key;
        size_t p = 0;
        if (!read_handle((const uint8_t*)e.value.data(), e.value.size(), &p,
                         &ie.offset, &ie.size))
            return false;
        index_.push_back(std::move(ie));
    }
    if (index_.empty())
        return false;
    // The table's largest key: shortest-successor of the last separator is
    // an upper bound hint; the exact largest key rides the last separator
    // itself (separator == last key when the builder did not shrink).
    last_key_ = index_.back().separator;
    return true;
}

bool TableReader::get(const std::string& internal, TableEntry* out) const {
    if (index_.empty())
        return false;
    // Seek the first separator ≥ target, per the table invariant
    // (separator_i ≥ every key in block i and < the first key of block i+1).
    size_t lo = 0, hi = index_.size();
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (internal_key_less(index_[mid].separator, internal))
            lo = mid + 1;
        else
            hi = mid;
    }
    // Target beyond every separator: the key, if present, lives in the last
    // block (the final separator is only an upper-bound hint) — leveldb
    // reads the last block in that case too.
    size_t block_idx = (lo < index_.size()) ? lo : index_.size() - 1;
    ParsedBlock block;
    if (!read_block(index_[block_idx].offset, index_[block_idx].size, &block))
        return false;
    // First entry ≥ target inside the block.
    auto it = std::lower_bound(block.entries.begin(), block.entries.end(), internal,
                               [](const TableEntry& e, const std::string& k) {
                                   return internal_key_less(e.key, k);
                               });
    if (it == block.entries.end())
        return false;
    if (user_key_of(it->key) != user_key_of(internal))
        return false;
    *out = *it;
    return true;
}

void TableReader::scan(std::vector<TableEntry>* out) const {
    for (const IndexEntry& ie : index_) {
        ParsedBlock block;
        if (!read_block(ie.offset, ie.size, &block))
            continue;
        out->insert(out->end(), block.entries.begin(), block.entries.end());
    }
}

} // namespace cursestone::world::format::leveldb::db
