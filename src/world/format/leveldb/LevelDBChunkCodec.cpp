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
// LevelDBChunkCodec.cpp — MCPE record codec implementation.
#include "LevelDBChunkCodec.h"

#include <cstdio>
#include <cstring>
#include <string>

#include "../../../nbt/NBT.h"
#include "../../../nbt/tag/Tag.h"

namespace cursestone::world::format::leveldb {
namespace {

// Offsets inside the 83200-byte legacy terrain record.
constexpr size_t kBlocksOff = 0;
constexpr size_t kDataOff = kBlocksOff + 32768;
constexpr size_t kSkyOff = kDataOff + 16384;
constexpr size_t kLightOff = kSkyOff + 16384;
constexpr size_t kHeightOff = kLightOff + 16384;
constexpr size_t kBiomeOff = kHeightOff + 256;

// Tag ids the stream walker needs.
constexpr uint8_t kNbtCompound = 0x0A;
constexpr uint8_t kNbtEnd = 0x00;
constexpr uint8_t kNbtByte = 0x01;
constexpr uint8_t kNbtShort = 0x02;
constexpr uint8_t kNbtInt = 0x03;
constexpr uint8_t kNbtLong = 0x04;
constexpr uint8_t kNbtFloat = 0x05;
constexpr uint8_t kNbtDouble = 0x06;
constexpr uint8_t kNbtByteArray = 0x07;
constexpr uint8_t kNbtString = 0x08;
constexpr uint8_t kNbtList = 0x09;
constexpr uint8_t kNbtIntArray = 0x0B;

// A tiny borrow-only cursor: the stream walker advances over raw bytes
// without copying anything.
struct Cursor {
    const uint8_t* p;
    const uint8_t* end;

    bool u8(uint8_t* v) {
        if (p >= end)
            return false;
        *v = *p++;
        return true;
    }
    bool u16(uint16_t* v) {
        if (end - p < 2)
            return false;
        *v = (uint16_t)(*p++) | ((uint16_t)(*p++) << 8);
        return true;
    }
    bool u32(uint32_t* v) {
        if (end - p < 4)
            return false;
        *v = (uint32_t)(*p++) | ((uint32_t)(*p++) << 8) | ((uint32_t)(*p++) << 16) |
             ((uint32_t)(*p++) << 24);
        return true;
    }
    bool skip(size_t n) {
        if ((size_t)(end - p) < n)
            return false;
        p += n;
        return true;
    }
    bool name() {
        uint16_t len = 0;
        if (!u16(&len))
            return false;
        return skip(len);
    }
};

bool skip_payload(Cursor* c, uint8_t type, int depth) {
    if (depth > 64)
        return false;
    switch (type) {
    case kNbtByte:
    case kNbtEnd:
        return c->skip(1);
    case kNbtShort:
        return c->skip(2);
    case kNbtInt:
    case kNbtFloat:
        return c->skip(4);
    case kNbtLong:
    case kNbtDouble:
        return c->skip(8);
    case kNbtByteArray:
    case kNbtIntArray: {
        uint32_t n = 0;
        if (!c->u32(&n))
            return false;
        return c->skip((size_t)n * (type == kNbtByteArray ? 1 : 4));
    }
    case kNbtString: {
        uint16_t n = 0;
        if (!c->u16(&n))
            return false;
        return c->skip(n);
    }
    case kNbtList: {
        uint8_t et = 0;
        uint32_t n = 0;
        if (!c->u8(&et) || !c->u32(&n))
            return false;
        if (et == kNbtEnd || n == 0)
            return true;
        for (uint32_t i = 0; i < n; ++i) {
            if (!skip_payload(c, et, depth + 1))
                return false;
        }
        return true;
    }
    case kNbtCompound: {
        for (;;) {
            uint8_t ct = 0;
            if (!c->u8(&ct))
                return false;
            if (ct == kNbtEnd)
                return true;
            if (!c->name())
                return false;
            if (!skip_payload(c, ct, depth + 1))
                return false;
        }
    }
    default:
        return false;
    }
}

bool read_whole_file(const std::string& path, std::vector<uint8_t>* out) {
    FILE* fp = std::fopen(path.c_str(), "rb");
    if (!fp)
        return false;
    std::fseek(fp, 0, SEEK_END);
    long sz = std::ftell(fp);
    std::fseek(fp, 0, SEEK_SET);
    if (sz <= 0) {
        std::fclose(fp);
        return false;
    }
    out->resize((size_t)sz);
    bool ok = (std::fread(out->data(), 1, (size_t)sz, fp) == (size_t)sz);
    std::fclose(fp);
    return ok;
}

} // namespace

// ---------------------------------------------------------------------------
// Chunk records
// ---------------------------------------------------------------------------

std::vector<uint8_t> LevelDBChunkCodec::record_key(int32_t cx, int32_t cz,
                                                   uint8_t record) {
    std::vector<uint8_t> key(9);
    uint32_t ux = (uint32_t)cx, uz = (uint32_t)cz;
    key[0] = (uint8_t)ux;
    key[1] = (uint8_t)(ux >> 8);
    key[2] = (uint8_t)(ux >> 16);
    key[3] = (uint8_t)(ux >> 24);
    key[4] = (uint8_t)uz;
    key[5] = (uint8_t)(uz >> 8);
    key[6] = (uint8_t)(uz >> 16);
    key[7] = (uint8_t)(uz >> 24);
    key[8] = record;
    return key;
}

mcp_chunk_t* LevelDBChunkCodec::decode_legacy_terrain(const uint8_t* data,
                                                      size_t len, int32_t cx,
                                                      int32_t cz) {
    // A record may be longer than the base layout only if a writer appended
    // extra data; the base part is what CurseStone consumes.
    if (len < kLegacyTerrainSize || !data)
        return nullptr;
    mcp_chunk_t* chunk = mcp_chunk_new(cx, cz);
    if (!chunk)
        return nullptr;

    // Column -> section transposition, the inverse of
    // chunk_wire_serialize_core: column index (x<<11 | z<<7 | y), section
    // index (x | z<<4 | y<<8).
    for (int x = 0; x < 16; ++x) {
        for (int z = 0; z < 16; ++z) {
            const uint8_t* col = data + ((size_t)x << 11) + ((size_t)z << 7);
            for (int sy = 0; sy < 8; ++sy) {
                mcp_chunk_section_t* s = mcp_chunk_get_or_alloc_section(chunk, (uint8_t)sy);
                if (!s) {
                    mcp_chunk_destroy(chunk);
                    return nullptr;
                }
                int y0 = sy << 4;
                // Nibble columns advance 8 bytes per section: the wire byte
                // for (x, z, section sy, pair i) sits at
                // ((x<<11 | z<<7 | y0) >> 1) + i = (x<<10) + (z<<6) + (sy<<3) + i.
                const uint8_t* col_data = data + kDataOff +
                                          ((size_t)x << 10) + ((size_t)z << 6) +
                                          ((size_t)sy << 3);
                const uint8_t* col_sky = data + kSkyOff +
                                         ((size_t)x << 10) + ((size_t)z << 6) +
                                         ((size_t)sy << 3);
                const uint8_t* col_light = data + kLightOff +
                                           ((size_t)x << 10) + ((size_t)z << 6) +
                                           ((size_t)sy << 3);
                // Section block index is (local_y<<8 | z<<4 | x): i is the
                // Y value inside THIS section (0..15) — the wire's absolute
                // Y (y0+i) must never index the 4096-cell section array.
                for (int i = 0; i < 16; ++i)
                    s->blocks[((size_t)i << 8) | ((size_t)(z & 15) << 4) |
                              (x & 15)] = col[y0 + i];
                // Nibbles: wire byte j packs Y (y0+2j, y0+2j+1) as (lo, hi);
                // the section byte packs X (2k, 2k+1) as (lo, hi) at the
                // same Y and Z — parity bit is x&1.
                for (int i = 0; i < 8; ++i) {
                    uint8_t w = col_data[i];
                    uint8_t pair[2] = {(uint8_t)(w & 0x0F), (uint8_t)(w >> 4)};
                    for (int half = 0; half < 2; ++half) {
                        int yl = y0 + 2 * i + half;
                        size_t idx = ((size_t)(yl & 15) << 8) | ((size_t)(z & 15) << 4) |
                                     (x & 15);
                        uint8_t v = pair[half];
                        if (x & 1)
                            s->data[idx >> 1] =
                                (uint8_t)((s->data[idx >> 1] & 0x0F) | (v << 4));
                        else
                            s->data[idx >> 1] =
                                (uint8_t)((s->data[idx >> 1] & 0xF0) | v);
                    }
                }
                for (int i = 0; i < 8; ++i) {
                    uint8_t w = col_sky[i];
                    uint8_t pair[2] = {(uint8_t)(w & 0x0F), (uint8_t)(w >> 4)};
                    for (int half = 0; half < 2; ++half) {
                        int yl = y0 + 2 * i + half;
                        size_t idx = ((size_t)(yl & 15) << 8) | ((size_t)(z & 15) << 4) |
                                     (x & 15);
                        uint8_t v = pair[half];
                        if (x & 1)
                            s->sky_light[idx >> 1] =
                                (uint8_t)((s->sky_light[idx >> 1] & 0x0F) | (v << 4));
                        else
                            s->sky_light[idx >> 1] =
                                (uint8_t)((s->sky_light[idx >> 1] & 0xF0) | v);
                    }
                }
                for (int i = 0; i < 8; ++i) {
                    uint8_t w = col_light[i];
                    uint8_t pair[2] = {(uint8_t)(w & 0x0F), (uint8_t)(w >> 4)};
                    for (int half = 0; half < 2; ++half) {
                        int yl = y0 + 2 * i + half;
                        size_t idx = ((size_t)(yl & 15) << 8) | ((size_t)(z & 15) << 4) |
                                     (x & 15);
                        uint8_t v = pair[half];
                        if (x & 1)
                            s->block_light[idx >> 1] =
                                (uint8_t)((s->block_light[idx >> 1] & 0x0F) | (v << 4));
                        else
                            s->block_light[idx >> 1] =
                                (uint8_t)((s->block_light[idx >> 1] & 0xF0) | v);
                    }
                }
            }
        }
    }

    // Heightmap: one byte per column.
    if (!chunk->height_map)
        chunk->height_map = (int32_t*)calloc(256, sizeof(int32_t));
    if (chunk->height_map) {
        for (int i = 0; i < 256; ++i)
            chunk->height_map[i] = data[kHeightOff + i];
    }
    // Biome colors: the high byte of each column int is the biome id.
    if (!chunk->biomes)
        chunk->biomes = (uint8_t*)calloc(256, 1);
    if (chunk->biomes) {
        for (int i = 0; i < 256; ++i)
            chunk->biomes[i] = data[kBiomeOff + (size_t)i * 4];
    }
    chunk->dirty = false;
    return chunk;
}

bool LevelDBChunkCodec::split_compound_stream(const uint8_t* data, size_t len,
                                              std::vector<std::vector<uint8_t>>* out) {
    out->clear();
    Cursor c{data, data + len};
    while (c.p < c.end) {
        const uint8_t* start = c.p;
        uint8_t type = 0;
        if (!c.u8(&type))
            return false;
        if (type != kNbtCompound)
            return false;
        if (!c.name())
            return false;
        if (!skip_payload(&c, type, 0))
            return false;
        out->emplace_back(start, c.p);
    }
    return true;
}

// ---------------------------------------------------------------------------
// level.dat
// ---------------------------------------------------------------------------

bool LevelDBChunkCodec::read_level_dat(const std::string& world_dir,
                                       struct cursestone::nbt::Tag** root,
                                       int32_t* storage_version) {
    std::vector<uint8_t> raw;
    if (!read_whole_file(world_dir + "/level.dat", &raw) &&
        !read_whole_file(world_dir + "/level.dat_old", &raw))
        return false;
    // Header: [int32 LE storage version][int32 LE NBT size][little-endian NBT].
    if (raw.size() < 8)
        return false;
    uint32_t version = (uint32_t)raw[0] | ((uint32_t)raw[1] << 8) |
                       ((uint32_t)raw[2] << 16) | ((uint32_t)raw[3] << 24);
    uint32_t size = (uint32_t)raw[4] | ((uint32_t)raw[5] << 8) |
                    ((uint32_t)raw[6] << 16) | ((uint32_t)raw[7] << 24);
    if (size == 0 || size > raw.size() - 8)
        size = (uint32_t)(raw.size() - 8); // tolerate a missing/lying size field
    cursestone::nbt::Tag* parsed = cursestone::nbt::read(raw.data() + 8, size,
                                                         /*little_endian=*/true);
    if (!parsed)
        return false;
    if (cursestone::nbt::type_of(parsed) != cursestone::nbt::TAG_COMPOUND) {
        delete parsed;
        return false;
    }
    *root = parsed;
    if (storage_version)
        *storage_version = (int32_t)version;
    return true;
}

bool LevelDBChunkCodec::write_level_dat(const std::string& world_dir,
                                        const struct cursestone::nbt::Tag* root) {
    std::vector<uint8_t> nbt;
    cursestone::nbt::write(root, /*little_endian=*/true, nbt);
    if (nbt.empty())
        return false;

    std::vector<uint8_t> file;
    uint32_t version = 3;
    uint32_t size = (uint32_t)nbt.size();
    file.reserve(8 + nbt.size());
    for (int i = 0; i < 4; ++i)
        file.push_back((uint8_t)(version >> (8 * i)));
    for (int i = 0; i < 4; ++i)
        file.push_back((uint8_t)(size >> (8 * i)));
    file.insert(file.end(), nbt.begin(), nbt.end());

    // Rotate first: the previous save becomes level.dat_old (the device's
    // behavior and the crash-recovery seam the real client reads back).
    std::string dat = world_dir + "/level.dat";
    std::rename(dat.c_str(), (world_dir + "/level.dat_old").c_str());

    std::string tmp = world_dir + "/level.dat.new";
    FILE* fp = std::fopen(tmp.c_str(), "wb");
    if (!fp)
        return false;
    bool ok = (std::fwrite(file.data(), 1, file.size(), fp) == file.size());
    std::fclose(fp);
    if (!ok) {
        std::remove(tmp.c_str());
        return false;
    }
    if (std::rename(tmp.c_str(), dat.c_str()) != 0) {
        std::remove(tmp.c_str());
        return false;
    }
    return true;
}

} // namespace cursestone::world::format::leveldb
