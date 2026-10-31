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
// AnvilChunkCodec.cpp — encodes/decodes mcp_chunk_t* to/from Nukkit
// Anvil chunk NBT (big-endian, Sections list based).
//
// Mirrors cn.nukkit.level.format.anvil.Chunk.toBinary / fromBinary
// (minus the ZLIB step, which is handled by AnvilRegionLoader).
#include "AnvilChunkCodec.h"
#include "world/format/ChunkSection.h"
#include "nbt/NBT.h"
#include "nbt/tag/CompoundTag.h"
#include "nbt/tag/ByteArrayTag.h"
#include "nbt/tag/IntArrayTag.h"
#include "nbt/tag/IntTag.h"
#include "nbt/tag/ByteTag.h"
#include "nbt/tag/LongTag.h"
#include "nbt/tag/ListTag.h"
#include "nbt/tag/Tag.h"

#include <stdlib.h>
#include <string.h>

// Only the lower 8 sections (0..7) are persisted — MCPE 0.15.x is 128-tall.
#define ANVIL_SECTIONS 8

// Helper: get a child compound of root by name (must be a CompoundTag).
static const cursestone::nbt::Tag* codec_get_compound(const cursestone::nbt::Tag* root, const char* name) {
    return cursestone::nbt::get_compound(root, name);
}

// Helper: get a byte-array child's data + length. Returns NULL if missing.
static const uint8_t* get_byte_array(const cursestone::nbt::Tag* root, const char* name, size_t* out_len) {
    const cursestone::nbt::Tag* t = cursestone::nbt::get(root, name);
    if (!t || cursestone::nbt::type_of(t) != cursestone::nbt::TAG_BYTE_ARRAY)
        return NULL;
    return cursestone::nbt::byte_array_data(t, out_len);
}

// Helper: get an int-array child's data + count.
static const int32_t* get_int_array(const cursestone::nbt::Tag* root, const char* name, size_t* out_count) {
    const cursestone::nbt::Tag* t = cursestone::nbt::get(root, name);
    if (!t || cursestone::nbt::type_of(t) != cursestone::nbt::TAG_INT_ARRAY)
        return NULL;
    return cursestone::nbt::int_array_data(t, out_count);
}

// =====================================================================
// ENCODE
// =====================================================================
std::vector<uint8_t> anvil_chunk_encode(const mcp_chunk_t* chunk) {
    if (!chunk)
        return {};

    // Root: CompoundTag("")
    auto* root = new cursestone::nbt::CompoundTag("");

    // World: CompoundTag
    cursestone::nbt::Tag* level = new cursestone::nbt::CompoundTag("Level");
    if (!level) {
        delete root;
        return {};
    }

    cursestone::nbt::set_int(level, "xPos", chunk->x);
    cursestone::nbt::set_int(level, "zPos", chunk->z);
    cursestone::nbt::set_long(level, "LastUpdate", chunk->last_update);
    cursestone::nbt::set_long(level, "InhabitedTime", 0);
    cursestone::nbt::set_byte(level, "vxl", 0);

    // Sections list.
    cursestone::nbt::Tag* sections = new cursestone::nbt::ListTag("Sections", cursestone::nbt::TAG_COMPOUND);
    for (int sy = 0; sy < ANVIL_SECTIONS; ++sy) {
        const mcp_chunk_section_t* s = chunk->sections ? chunk->sections[sy] : NULL;
        if (!s)
            continue;
        // Skip fully-empty sections (matching Nukkit's EmptyChunkSection handling).
        bool has_blocks = false;
        if (s->blocks) {
            for (size_t i = 0; i < 4096; ++i) {
                if (s->blocks[i] != 0) {
                    has_blocks = true;
                    break;
                }
            }
        }
        if (!has_blocks)
            continue;

        cursestone::nbt::Tag* sec = new cursestone::nbt::CompoundTag("");
        cursestone::nbt::set_byte(sec, "Y", (int8_t)sy);

        // Blocks (4096) — section is already YZX order: (y<<8)|(z<<4)|x.
        cursestone::nbt::set(sec, "Blocks", new cursestone::nbt::ByteArrayTag("Blocks", s->blocks, 4096));

        // Data (2048)
        if (s->data) {
            cursestone::nbt::set(sec, "Data", new cursestone::nbt::ByteArrayTag("Data", s->data, 2048));
        } else {
            uint8_t* zero = (uint8_t*)calloc(2048, 1);
            cursestone::nbt::set(sec, "Data", new cursestone::nbt::ByteArrayTag("Data", zero, 2048));
            free(zero);
        }

        // BlockLight (2048)
        if (s->block_light) {
            cursestone::nbt::set(sec, "BlockLight", new cursestone::nbt::ByteArrayTag("BlockLight", s->block_light, 2048));
        } else {
            uint8_t* zero = (uint8_t*)calloc(2048, 1);
            cursestone::nbt::set(sec, "BlockLight", new cursestone::nbt::ByteArrayTag("BlockLight", zero, 2048));
            free(zero);
        }

        // SkyLight (2048) — default to 0xFF if not present.
        if (s->sky_light) {
            cursestone::nbt::set(sec, "SkyLight", new cursestone::nbt::ByteArrayTag("SkyLight", s->sky_light, 2048));
        } else {
            uint8_t* full = (uint8_t*)malloc(2048);
            if (full)
                memset(full, 0xFF, 2048);
            cursestone::nbt::set(sec, "SkyLight", new cursestone::nbt::ByteArrayTag("SkyLight", full ? full : NULL, 2048));
            free(full);
        }

        cursestone::nbt::append(sections, sec);
    }
    cursestone::nbt::set(level, "Sections", sections);

    // BiomeColors: 256 ints (one per column). For MCPE compatibility we
    // store biome byte at the lowest byte of each int.
    if (chunk->biomes) {
        int32_t* bc = (int32_t*)malloc(256 * sizeof(int32_t));
        if (bc) {
            for (int i = 0; i < 256; ++i) {
                bc[i] = (int32_t)chunk->biomes[i];
            }
            cursestone::nbt::set(level, "BiomeColors", new cursestone::nbt::IntArrayTag("BiomeColors", bc, 256));
            free(bc);
        }
    } else {
        int32_t* bc = (int32_t*)calloc(256, sizeof(int32_t));
        cursestone::nbt::set(level, "BiomeColors", new cursestone::nbt::IntArrayTag("BiomeColors", bc, 256));
        free(bc);
    }

    // HeightMap: 256 ints.
    if (chunk->height_map) {
        cursestone::nbt::set(level, "HeightMap", new cursestone::nbt::IntArrayTag("HeightMap", chunk->height_map, 256));
    } else {
        int32_t* hm = (int32_t*)calloc(256, sizeof(int32_t));
        cursestone::nbt::set(level, "HeightMap", new cursestone::nbt::IntArrayTag("HeightMap", hm, 256));
        free(hm);
    }

    // ExtraData: empty (Nukkit format: var-int count + entries; we
    // store a zero-length int array which decoders treat as "no extras").
    {
        int32_t zero = 0;
        cursestone::nbt::set(level, "ExtraData", new cursestone::nbt::IntArrayTag("ExtraData", &zero, 0));
    }

    // Entities list.
    cursestone::nbt::Tag* entities_list = new cursestone::nbt::ListTag("Entities", cursestone::nbt::TAG_COMPOUND);
    for (size_t i = 0; i < chunk->entity_count; ++i) {
        if (!chunk->entities || !chunk->entity_lens)
            continue;
        const uint8_t* blob = chunk->entities[i];
        size_t blen = chunk->entity_lens[i];
        if (!blob || blen == 0)
            continue;
        // Stored blobs are little-endian (legacy). Re-serialize as big-endian.
        cursestone::nbt::Tag* t = cursestone::nbt::read(blob, blen, /*little_endian=*/true);
        if (t) {
            cursestone::nbt::set_name(t, "");
            cursestone::nbt::append(entities_list, t);
        }
    }
    cursestone::nbt::set(level, "Entities", entities_list);

    // TileEntities list.
    cursestone::nbt::Tag* tiles_list = new cursestone::nbt::ListTag("TileEntities", cursestone::nbt::TAG_COMPOUND);
    for (size_t i = 0; i < chunk->tile_entity_count; ++i) {
        if (!chunk->tile_entities || !chunk->tile_entity_lens)
            continue;
        const uint8_t* blob = chunk->tile_entities[i];
        size_t blen = chunk->tile_entity_lens[i];
        if (!blob || blen == 0)
            continue;
        cursestone::nbt::Tag* t = cursestone::nbt::read(blob, blen, /*little_endian=*/true);
        if (t) {
            cursestone::nbt::set_name(t, "");
            cursestone::nbt::append(tiles_list, t);
        }
    }
    cursestone::nbt::set(level, "TileEntities", tiles_list);

    // Terrain flags.
    cursestone::nbt::set_byte(level, "TerrainGenerated", chunk->terrain_populated ? 1 : 0);
    cursestone::nbt::set_byte(level, "TerrainPopulated", chunk->terrain_populated ? 1 : 0);
    cursestone::nbt::set_byte(level, "LightPopulated", chunk->light_populated ? 1 : 0);

    cursestone::nbt::set(root, "Level", level);

    // Serialize big-endian.
    std::vector<uint8_t> buf;
    cursestone::nbt::write(root, /*little_endian=*/false, buf);
    delete root;
    return buf;
}

// =====================================================================
// DECODE
// =====================================================================
mcp_chunk_t* anvil_chunk_decode(const uint8_t* data, size_t len) {
    if (!data || len == 0)
        return NULL;

    cursestone::nbt::Tag* root = cursestone::nbt::read(data, len, /*little_endian=*/false);
    if (!root)
        return NULL;
    if (cursestone::nbt::type_of(root) != cursestone::nbt::TAG_COMPOUND) {
        delete root;
        return NULL;
    }

    const cursestone::nbt::Tag* level = codec_get_compound(root, "Level");
    if (!level) {
        delete root;
        return NULL;
    }

    int32_t cx = cursestone::nbt::get_int(level, "xPos", 0);
    int32_t cz = cursestone::nbt::get_int(level, "zPos", 0);
    mcp_chunk_t* chunk = mcp_chunk_new(cx, cz);
    if (!chunk) {
        delete root;
        return NULL;
    }

    chunk->last_update = cursestone::nbt::get_long(level, "LastUpdate", 0);
    chunk->terrain_populated = cursestone::nbt::get_byte(level, "TerrainPopulated", 0) != 0 ||
                               cursestone::nbt::get_byte(level, "TerrainGenerated", 0) != 0;
    chunk->light_populated = cursestone::nbt::get_byte(level, "LightPopulated", 0) != 0;

    // Sections list.
    const cursestone::nbt::Tag* sections = cursestone::nbt::get(level, "Sections");
    if (sections && cursestone::nbt::type_of(sections) == cursestone::nbt::TAG_LIST) {
        for (size_t i = 0, n = cursestone::nbt::size(sections); i < n; ++i) {
            const cursestone::nbt::Tag* sec = cursestone::nbt::at(sections, i);
            if (!sec || cursestone::nbt::type_of(sec) != cursestone::nbt::TAG_COMPOUND)
                continue;
            int8_t y_idx = cursestone::nbt::get_byte(sec, "Y", -1);
            if (y_idx < 0 || y_idx >= ANVIL_SECTIONS)
                continue;
            mcp_chunk_section_t* s = mcp_chunk_get_or_alloc_section(chunk, (uint8_t)y_idx);
            if (!s)
                continue;

            size_t bl_len = 0;
            const uint8_t* bl = get_byte_array(sec, "Blocks", &bl_len);
            if (bl && bl_len >= 4096 && s->blocks) {
                memcpy(s->blocks, bl, 4096);
            }

            size_t d_len = 0;
            const uint8_t* d = get_byte_array(sec, "Data", &d_len);
            if (d && d_len >= 2048) {
                if (!s->data)
                    s->data = (uint8_t*)calloc(2048, 1);
                if (s->data)
                    memcpy(s->data, d, 2048);
            }

            size_t bl2_len = 0;
            const uint8_t* bl2 = get_byte_array(sec, "BlockLight", &bl2_len);
            if (bl2 && bl2_len >= 2048) {
                if (!s->block_light)
                    s->block_light = (uint8_t*)calloc(2048, 1);
                if (s->block_light)
                    memcpy(s->block_light, bl2, 2048);
            }

            size_t sl_len = 0;
            const uint8_t* sl = get_byte_array(sec, "SkyLight", &sl_len);
            if (sl && sl_len >= 2048) {
                if (!s->sky_light)
                    s->sky_light = (uint8_t*)calloc(2048, 1);
                if (s->sky_light)
                    memcpy(s->sky_light, sl, 2048);
            } else {
                // Default to full bright.
                if (!s->sky_light)
                    s->sky_light = (uint8_t*)malloc(2048);
                if (s->sky_light)
                    memset(s->sky_light, 0xFF, 2048);
            }
        }
    }

    // BiomeColors → biomes (256 bytes).
    size_t bc_count = 0;
    const int32_t* bc = get_int_array(level, "BiomeColors", &bc_count);
    if (bc && bc_count >= 256) {
        if (!chunk->biomes)
            chunk->biomes = (uint8_t*)calloc(256, 1);
        if (chunk->biomes) {
            for (int i = 0; i < 256; ++i) {
                chunk->biomes[i] = (uint8_t)(bc[i] & 0xFF);
            }
        }
    }

    // HeightMap: 256 ints.
    size_t hm_count = 0;
    const int32_t* hm = get_int_array(level, "HeightMap", &hm_count);
    if (hm && hm_count >= 256) {
        if (!chunk->height_map)
            chunk->height_map = (int32_t*)calloc(256, sizeof(int32_t));
        if (chunk->height_map) {
            for (int i = 0; i < 256; ++i) {
                chunk->height_map[i] = hm[i];
            }
        }
    }

    // Tile entities.
    const cursestone::nbt::Tag* tiles_list = cursestone::nbt::get(level, "TileEntities");
    if (tiles_list && cursestone::nbt::type_of(tiles_list) == cursestone::nbt::TAG_LIST) {
        for (size_t i = 0, n = cursestone::nbt::size(tiles_list); i < n; ++i) {
            const cursestone::nbt::Tag* t = cursestone::nbt::at(tiles_list, i);
            if (!t)
                continue;
            std::vector<uint8_t> bbuf;
            cursestone::nbt::write(t, /*little_endian=*/true, bbuf);
            if (!bbuf.empty()) {
                mcp_chunk_add_tile_entity(chunk, bbuf.data(), bbuf.size());
            }
        }
    }

    // Entities.
    const cursestone::nbt::Tag* entities_list = cursestone::nbt::get(level, "Entities");
    if (entities_list && cursestone::nbt::type_of(entities_list) == cursestone::nbt::TAG_LIST) {
        for (size_t i = 0, n = cursestone::nbt::size(entities_list); i < n; ++i) {
            const cursestone::nbt::Tag* t = cursestone::nbt::at(entities_list, i);
            if (!t)
                continue;
            std::vector<uint8_t> bbuf;
            cursestone::nbt::write(t, /*little_endian=*/true, bbuf);
            if (!bbuf.empty()) {
                mcp_chunk_add_entity(chunk, bbuf.data(), bbuf.size());
            }
        }
    }

    delete root;
    chunk->dirty = false;
    return chunk;
}
