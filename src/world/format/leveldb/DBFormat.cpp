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
// DBFormat.cpp — implementation of the LevelDB byte-level primitives.
#include "DBFormat.h"

#include <cstring>
#include <zlib.h>

namespace cursestone::world::format::leveldb::db {
namespace {

// Castagnoli polynomial (reflected), built once.
struct Crc32cTable {
    uint32_t t[256];
    Crc32cTable() {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? (0x82f63b78u ^ (c >> 1)) : (c >> 1);
            t[i] = c;
        }
    }
};
const Crc32cTable kCrcTable;

// Snappy block tags.
constexpr uint32_t kSnappyLiteral = 0;
constexpr uint32_t kSnappyCopy1 = 1;
constexpr uint32_t kSnappyCopy2 = 2;
constexpr uint32_t kSnappyCopy4 = 3;

bool snappy_read_varint(const uint8_t* in, size_t in_len, size_t* pos,
                        uint32_t* out) {
    uint32_t result = 0;
    int shift = 0;
    while (*pos < in_len) {
        uint8_t b = in[(*pos)++];
        result |= (uint32_t)(b & 0x7F) << shift;
        if (!(b & 0x80)) {
            *out = result;
            return true;
        }
        shift += 7;
        if (shift > 28)
            return false; // >5 bytes: not a snappy varint
    }
    return false;
}

} // namespace

bool get_var_u32(const uint8_t* data, size_t len, size_t* pos, uint32_t* out) {
    uint32_t result = 0;
    int shift = 0;
    while (true) {
        if (*pos >= len)
            return false;
        uint8_t b = data[(*pos)++];
        result |= (uint32_t)(b & 0x7F) << shift;
        if (!(b & 0x80)) {
            *out = result;
            return true;
        }
        shift += 7;
        if (shift > 31)
            return false;
    }
}

bool get_var_u64(const uint8_t* data, size_t len, size_t* pos, uint64_t* out) {
    uint64_t result = 0;
    int shift = 0;
    while (true) {
        if (*pos >= len)
            return false;
        uint8_t b = data[(*pos)++];
        result |= (uint64_t)(b & 0x7F) << shift;
        if (!(b & 0x80)) {
            *out = result;
            return true;
        }
        shift += 7;
        if (shift > 63)
            return false;
    }
}

uint32_t crc32c(const uint8_t* data, size_t len, uint32_t seed) {
    uint32_t c = seed ^ 0xffffffffu;
    for (size_t i = 0; i < len; ++i)
        c = kCrcTable.t[(c ^ data[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xffffffffu;
}

bool snappy_decompress(const uint8_t* in, size_t in_len, std::vector<uint8_t>* out) {
    size_t pos = 0;
    uint32_t expected_len = 0;
    if (!snappy_read_varint(in, in_len, &pos, &expected_len))
        return false;
    if (expected_len == 0) {
        out->clear();
        return pos == in_len;
    }
    if (expected_len > kMaxBlockInflate)
        return false;
    out->clear();
    out->reserve(expected_len);

    while (pos < in_len) {
        uint8_t tag_byte = in[pos++];
        uint32_t tag = tag_byte & 3;
        if (tag == kSnappyLiteral) {
            // Length: 0x3C and below is the length itself; 0x3D/3E/3F add
            // 1/2/4 extra bytes (the 60/61/62 variants in the spec; 63 is
            // unused). CurseStone maps on the spec names: 60/61/62/63.
            uint32_t length = tag_byte >> 2;
            if (length < 60) {
                length += 1;
            } else {
                uint32_t extra = length - 59; // 1..4 bytes
                if (pos + extra > in_len)
                    return false;
                length = 0;
                for (uint32_t i = 0; i < extra; ++i)
                    length |= (uint32_t)in[pos + i] << (8 * i);
                length += 1;
                pos += extra;
            }
            if (length > in_len - pos || out->size() + length > expected_len)
                return false;
            out->insert(out->end(), in + pos, in + pos + length);
            pos += length;
        } else {
            // Copy: length and offset encoding differ per tag width.
            uint32_t length = 0, offset = 0;
            if (tag == kSnappyCopy1) {
                if (pos >= in_len)
                    return false;
                length = 4 + ((tag_byte >> 2) & 0x7);
                offset = ((uint32_t)(tag_byte >> 5) << 8) | in[pos];
                pos += 1;
            } else if (tag == kSnappyCopy2) {
                if (pos + 2 > in_len)
                    return false;
                length = 1 + (tag_byte >> 2);
                offset = (uint32_t)in[pos] | ((uint32_t)in[pos + 1] << 8);
                pos += 2;
            } else {
                if (pos + 4 > in_len)
                    return false;
                length = 1 + (tag_byte >> 2);
                offset = (uint32_t)in[pos] | ((uint32_t)in[pos + 1] << 8) |
                         ((uint32_t)in[pos + 2] << 16) | ((uint32_t)in[pos + 3] << 24);
                pos += 4;
            }
            if (offset == 0 || offset > out->size())
                return false;
            if (out->size() + length > expected_len)
                return false;
            // Byte-by-byte (offset may overlap the write window).
            size_t src = out->size() - offset;
            for (uint32_t i = 0; i < length; ++i)
                out->push_back((*out)[src + i]);
        }
    }
    return out->size() == expected_len;
}

// ---------------------------------------------------------------------------
// Log records
// ---------------------------------------------------------------------------

void log_append_record(std::vector<uint8_t>* file_bytes, const uint8_t* payload,
                       size_t len, uint64_t start_offset) {
    size_t block_left;
    size_t pos = 0;
    bool first_fragment = true;
    do {
        size_t block_offset = (start_offset + file_bytes->size()) % kLogBlockSize;
        block_left = kLogBlockSize - block_offset;
        if (block_left < 7) {
            // Trailing fragment too small for a header: pad with zeros and
            // start the next block.
            file_bytes->insert(file_bytes->end(), block_left, 0);
            continue;
        }
        size_t avail = kLogBlockSize - block_offset - 7;
        size_t fragment = (len - pos < avail) ? (len - pos) : avail;

        bool begin = first_fragment;
        bool end = (pos + fragment == len);
        uint8_t type;
        if (begin && end)
            type = kLogFull;
        else if (begin)
            type = kLogFirst;
        else if (end)
            type = kLogLast;
        else
            type = kLogMiddle;

        uint8_t header[7];
        // 7-byte header: len(2, LE) + type(1) + masked CRC32C(4, LE) over
        // (type byte || payload) — exactly leveldb's chain order.
        uint16_t flen = (uint16_t)fragment;
        header[0] = (uint8_t)(flen & 0xFF);
        header[1] = (uint8_t)(flen >> 8);
        header[2] = type;
        uint32_t crc = crc32c(payload + pos, fragment, crc32c(&header[2], 1));
        crc = mask_crc(crc);
        header[3] = (uint8_t)crc;
        header[4] = (uint8_t)(crc >> 8);
        header[5] = (uint8_t)(crc >> 16);
        header[6] = (uint8_t)(crc >> 24);
        file_bytes->insert(file_bytes->end(), header, header + 7);
        file_bytes->insert(file_bytes->end(), payload + pos, payload + pos + fragment);
        pos += fragment;
        first_fragment = false;
    } while (pos < len);
}

bool LogReader::next(const uint8_t** payload, size_t* len) {
    static thread_local std::vector<uint8_t> scratch;
    scratch.clear();
    bool in_fragment = false;
    while (true) {
        if (pos_ >= image_.size())
            return false;
        size_t block_offset = pos_ % kLogBlockSize;
        size_t block_left = kLogBlockSize - block_offset;
        if (block_left < 7) {
            // Padding after a header that did not fit: skip the remainder.
            pos_ += block_left;
            continue;
        }
        if (image_.size() - pos_ < 7)
            return false; // torn header: treat as EOF
        const uint8_t* h = image_.data() + pos_;
        uint16_t flen = (uint16_t)h[0] | ((uint16_t)h[1] << 8);
        uint8_t type = h[2];
        if (flen > block_left - 7)
            return false; // length overruns the block: corruption
        const uint8_t* body = h + 7;
        uint32_t crc = crc32c(body, flen, crc32c(&type, 1));
        if (mask_crc(crc) !=
            ((uint32_t)h[3] | ((uint32_t)h[4] << 8) | ((uint32_t)h[5] << 16) |
             ((uint32_t)h[6] << 24)))
            return false; // CRC mismatch: torn tail, stop here
        pos_ += 7 + flen;
        switch (type) {
        case kLogFull:
            if (in_fragment)
                return false;
            *payload = body;
            *len = flen;
            return true;
        case kLogFirst:
            if (in_fragment)
                return false;
            in_fragment = true;
            scratch.assign(body, body + flen);
            break;
        case kLogMiddle:
            if (!in_fragment)
                return false;
            scratch.insert(scratch.end(), body, body + flen);
            break;
        case kLogLast:
            if (!in_fragment)
                return false;
            scratch.insert(scratch.end(), body, body + flen);
            *payload = scratch.data();
            *len = scratch.size();
            return true;
        default:
            return false; // unknown record type (recycled formats): EOF
        }
    }
}

// ---------------------------------------------------------------------------
// Internal keys
// ---------------------------------------------------------------------------

bool internal_key_less(const std::string& a, const std::string& b) {
    size_t ua = a.size() >= 8 ? a.size() - 8 : a.size();
    size_t ub = b.size() >= 8 ? b.size() - 8 : b.size();
    size_t common = (ua < ub) ? ua : ub;
    int cmp = std::memcmp(a.data(), b.data(), common);
    if (cmp != 0)
        return cmp < 0;
    if (ua != ub)
        return ua < ub;
    // Same user key: newest first — larger sequence, then larger type.
    uint64_t sa = sequence_of(a), sb = sequence_of(b);
    if (sa != sb)
        return sa > sb;
    return type_of(a) > type_of(b);
}

std::string find_shortest_separator(const std::string& start, const std::string& limit) {
    // Compare user parts.
    std::string ustart = user_key_of(start);
    std::string ulimit = user_key_of(limit);
    size_t common = (ustart.size() < ulimit.size()) ? ustart.size() : ulimit.size();
    size_t diff_at = common;
    for (size_t i = 0; i < common; ++i) {
        if ((uint8_t)ustart[i] != (uint8_t)ulimit[i]) {
            diff_at = i;
            break;
        }
    }
    if (diff_at < common && (uint8_t)ustart[diff_at] != 0xFF &&
        (uint8_t)ustart[diff_at] + 1 < (uint8_t)ulimit[diff_at]) {
        // Byte can be bumped: everything after it drops.
        ustart.resize(diff_at + 1);
        ustart[diff_at] = (char)((uint8_t)ustart[diff_at] + 1);
        return make_internal_key(ustart, kMaxSequenceNumber, kTypeValue);
    }
    // Differs only after `common` (or not at all): a longer start user key
    // can be cut to the common length + 1 when that still sorts before limit.
    if (ustart.size() > common && diff_at == common && common < ulimit.size()) {
        return make_internal_key(ustart.substr(0, common + 1), kMaxSequenceNumber,
                                 kTypeValue);
    }
    return start;
}

std::string find_short_successor(const std::string& key) {
    std::string u = user_key_of(key);
    for (size_t i = 0; i < u.size(); ++i) {
        if ((uint8_t)u[i] != 0xFF) {
            u[i] = (char)((uint8_t)u[i] + 1);
            u.resize(i + 1);
            return make_internal_key(u, 0, kTypeValue);
        }
    }
    return key; // all 0xFF: keep as is
}

// ---------------------------------------------------------------------------
// VersionEdit
// ---------------------------------------------------------------------------

void VersionEdit::encode(std::vector<uint8_t>* out) const {
    if (!comparator.empty()) {
        put_var_u32(out, kTagComparator);
        put_var_u32(out, (uint32_t)comparator.size());
        out->insert(out->end(), comparator.begin(), comparator.end());
    }
    if (prev_log_number != 0) {
        put_var_u32(out, kTagPrevLogNumber);
        put_var_u64(out, prev_log_number);
    }
    put_var_u32(out, kTagLogNumber);
    put_var_u64(out, log_number);
    put_var_u32(out, kTagNextFileNumber);
    put_var_u64(out, next_file_number);
    put_var_u32(out, kTagLastSequence);
    put_var_u64(out, last_sequence);
    for (uint64_t df : deleted_files) {
        put_var_u32(out, kTagDeletedFile);
        put_var_u32(out, (uint32_t)(df >> 32));
        put_var_u32(out, (uint32_t)(df & 0xFFFFFFFFu));
    }
    for (const FileMeta& f : added_files) {
        put_var_u32(out, kTagNewFile);
        put_var_u32(out, (uint32_t)f.level);
        put_var_u64(out, f.number);
        put_var_u64(out, f.size);
        put_var_u32(out, (uint32_t)f.smallest.size());
        out->insert(out->end(), f.smallest.begin(), f.smallest.end());
        put_var_u32(out, (uint32_t)f.largest.size());
        out->insert(out->end(), f.largest.begin(), f.largest.end());
    }
}

bool VersionEdit::decode(const uint8_t* data, size_t len) {
    size_t pos = 0;
    while (pos < len) {
        uint32_t tag = 0;
        if (!get_var_u32(data, len, &pos, &tag))
            return false;
        switch (tag) {
        case kTagComparator: {
            uint32_t n = 0;
            if (!get_var_u32(data, len, &pos, &n) || pos + n > len)
                return false;
            comparator.assign((const char*)data + pos, n);
            pos += n;
            break;
        }
        case kTagLogNumber:
            if (!get_var_u64(data, len, &pos, &log_number))
                return false;
            break;
        case kTagPrevLogNumber:
            if (!get_var_u64(data, len, &pos, &prev_log_number))
                return false;
            break;
        case kTagNextFileNumber:
            if (!get_var_u64(data, len, &pos, &next_file_number))
                return false;
            break;
        case kTagLastSequence:
            if (!get_var_u64(data, len, &pos, &last_sequence))
                return false;
            break;
        case kTagDeletedFile: {
            uint32_t level = 0, number = 0;
            if (!get_var_u32(data, len, &pos, &level) ||
                !get_var_u32(data, len, &pos, &number))
                return false;
            deleted_files.push_back(((uint64_t)level << 32) | number);
            break;
        }
        case kTagNewFile: {
            FileMeta f;
            uint32_t n = 0;
            uint32_t level = 0;
            if (!get_var_u32(data, len, &pos, &level) ||
                !get_var_u64(data, len, &pos, &f.number) ||
                !get_var_u64(data, len, &pos, &f.size) ||
                !get_var_u32(data, len, &pos, &n) || pos + n > len)
                return false;
            f.level = (int)level;
            f.smallest.assign((const char*)data + pos, n);
            pos += n;
            if (!get_var_u32(data, len, &pos, &n) || pos + n > len)
                return false;
            f.largest.assign((const char*)data + pos, n);
            pos += n;
            added_files.push_back(std::move(f));
            break;
        }
        case kTagCompactPointer: {
            // Present in real manifests; carries no data we need.
            uint32_t level = 0, n = 0;
            if (!get_var_u32(data, len, &pos, &level) ||
                !get_var_u32(data, len, &pos, &n) || pos + n > len)
                return false;
            pos += n;
            break;
        }
        default:
            return false; // unknown tag: refuse (leveldb does the same)
        }
    }
    return true;
}

} // namespace cursestone::world::format::leveldb::db
