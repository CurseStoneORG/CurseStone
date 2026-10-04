// SPDX-License-Identifier: LGPL-3.0-or-later
// AnvilChunkCodec.c — encodes/decodes mcp_chunk_t* to/from Nukkit
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
static const mcp_tag_t* get_compound(const mcp_tag_t* root, const char* name) {
    return mcp_compound_tag_get_compound(root, name);
}

// Helper: get a byte-array child's data + length. Returns NULL if missing.
static const uint8_t* get_byte_array(const mcp_tag_t* root, const char* name,
                                       size_t* out_len) {
    const mcp_tag_t* t = mcp_compound_tag_get(root, name);
    if (!t) return NULL;
    if (mcp_tag_get_type(t) != MCP_TAG_BYTE_ARRAY) return NULL;
    const mcp_byte_array_tag_t* ba = (const mcp_byte_array_tag_t*)t;
    if (out_len) *out_len = ba->length;
    return ba->data;
}

// Helper: get an int-array child's data + count.
static const int32_t* get_int_array(const mcp_tag_t* root, const char* name,
                                       size_t* out_count) {
    const mcp_tag_t* t = mcp_compound_tag_get(root, name);
    if (!t) return NULL;
    if (mcp_tag_get_type(t) != MCP_TAG_INT_ARRAY) return NULL;
    const mcp_int_array_tag_t* ia = (const mcp_int_array_tag_t*)t;
    if (out_count) *out_count = ia->count;
    return ia->data;
}

// =====================================================================
// ENCODE
// =====================================================================
uint8_t* anvil_chunk_encode(const mcp_chunk_t* chunk, size_t* out_len) {
    if (!chunk || !out_len) return NULL;
    *out_len = 0;

    // Root: CompoundTag("")
    mcp_tag_t* root = mcp_compound_tag_new_named("");
    if (!root) return NULL;

    // World: CompoundTag
    mcp_tag_t* level = mcp_compound_tag_new_named("Level");
    if (!level) { mcp_tag_destroy(root); return NULL; }

    mcp_compound_tag_set_int(level, "xPos", chunk->x);
    mcp_compound_tag_set_int(level, "zPos", chunk->z);
    mcp_compound_tag_set_long(level, "LastUpdate", chunk->last_update);
    mcp_compound_tag_set_long(level, "InhabitedTime", 0);
    mcp_compound_tag_set_byte(level, "vxl", 0);

    // Sections list.
    mcp_tag_t* sections = mcp_list_tag_new_named("Sections", MCP_TAG_COMPOUND);
    for (int sy = 0; sy < ANVIL_SECTIONS; ++sy) {
        const mcp_chunk_section_t* s = chunk->sections ? chunk->sections[sy] : NULL;
        if (!s) continue;
        // Skip fully-empty sections (matching Nukkit's EmptyChunkSection handling).
        bool has_blocks = false;
        if (s->blocks) {
            for (size_t i = 0; i < 4096; ++i) {
                if (s->blocks[i] != 0) { has_blocks = true; break; }
            }
        }
        if (!has_blocks) continue;

        mcp_tag_t* sec = mcp_compound_tag_new_named("");
        mcp_compound_tag_set_byte(sec, "Y", (int8_t)sy);

        // Blocks (4096) — section is already YZX order: (y<<8)|(z<<4)|x.
        mcp_compound_tag_set(sec, "Blocks",
            mcp_byte_array_tag_new_named("Blocks", s->blocks, 4096));

        // Data (2048)
        if (s->data) {
            mcp_compound_tag_set(sec, "Data",
                mcp_byte_array_tag_new_named("Data", s->data, 2048));
        } else {
            uint8_t* zero = (uint8_t*)calloc(2048, 1);
            mcp_compound_tag_set(sec, "Data",
                mcp_byte_array_tag_new_named("Data", zero, 2048));
            free(zero);
        }

        // BlockLight (2048)
        if (s->block_light) {
            mcp_compound_tag_set(sec, "BlockLight",
                mcp_byte_array_tag_new_named("BlockLight", s->block_light, 2048));
        } else {
            uint8_t* zero = (uint8_t*)calloc(2048, 1);
            mcp_compound_tag_set(sec, "BlockLight",
                mcp_byte_array_tag_new_named("BlockLight", zero, 2048));
            free(zero);
        }

        // SkyLight (2048) — default to 0xFF if not present.
        if (s->sky_light) {
            mcp_compound_tag_set(sec, "SkyLight",
                mcp_byte_array_tag_new_named("SkyLight", s->sky_light, 2048));
        } else {
            uint8_t* full = (uint8_t*)malloc(2048);
            if (full) memset(full, 0xFF, 2048);
            mcp_compound_tag_set(sec, "SkyLight",
                mcp_byte_array_tag_new_named("SkyLight", full ? full : NULL, 2048));
            free(full);
        }

        mcp_list_tag_append(sections, sec);
    }
    mcp_compound_tag_set(level, "Sections", sections);

    // BiomeColors: 256 ints (one per column). For MCPE compatibility we
    // store biome byte at the lowest byte of each int.
    if (chunk->biomes) {
        int32_t* bc = (int32_t*)malloc(256 * sizeof(int32_t));
        if (bc) {
            for (int i = 0; i < 256; ++i) {
                bc[i] = (int32_t)chunk->biomes[i];
            }
            mcp_compound_tag_set(level, "BiomeColors",
                mcp_int_array_tag_new_named("BiomeColors", bc, 256));
            free(bc);
        }
    } else {
        int32_t* bc = (int32_t*)calloc(256, sizeof(int32_t));
        mcp_compound_tag_set(level, "BiomeColors",
            mcp_int_array_tag_new_named("BiomeColors", bc, 256));
        free(bc);
    }

    // HeightMap: 256 ints.
    if (chunk->height_map) {
        mcp_compound_tag_set(level, "HeightMap",
            mcp_int_array_tag_new_named("HeightMap", chunk->height_map, 256));
    } else {
        int32_t* hm = (int32_t*)calloc(256, sizeof(int32_t));
        mcp_compound_tag_set(level, "HeightMap",
            mcp_int_array_tag_new_named("HeightMap", hm, 256));
        free(hm);
    }

    // ExtraData: empty (Nukkit format: var-int count + entries; we
    // store a zero-length int array which decoders treat as "no extras").
    {
        int32_t zero = 0;
        mcp_compound_tag_set(level, "ExtraData",
            mcp_int_array_tag_new_named("ExtraData", &zero, 0));
    }

    // Entities list.
    mcp_tag_t* entities_list = mcp_list_tag_new_named("Entities", MCP_TAG_COMPOUND);
    for (size_t i = 0; i < chunk->entity_count; ++i) {
        if (!chunk->entities || !chunk->entity_lens) continue;
        const uint8_t* blob = chunk->entities[i];
        size_t blen = chunk->entity_lens[i];
        if (!blob || blen == 0) continue;
        // Stored blobs are little-endian (legacy). Re-serialize as big-endian.
        mcp_tag_t* t = mcp_nbt_read_bytes(blob, blen, /*little_endian=*/true);
        if (t) {
            mcp_tag_set_name_cstr(t, "");
            mcp_list_tag_append(entities_list, t);
        }
    }
    mcp_compound_tag_set(level, "Entities", entities_list);

    // TileEntities list.
    mcp_tag_t* tiles_list = mcp_list_tag_new_named("TileEntities", MCP_TAG_COMPOUND);
    for (size_t i = 0; i < chunk->tile_entity_count; ++i) {
        if (!chunk->tile_entities || !chunk->tile_entity_lens) continue;
        const uint8_t* blob = chunk->tile_entities[i];
        size_t blen = chunk->tile_entity_lens[i];
        if (!blob || blen == 0) continue;
        mcp_tag_t* t = mcp_nbt_read_bytes(blob, blen, /*little_endian=*/true);
        if (t) {
            mcp_tag_set_name_cstr(t, "");
            mcp_list_tag_append(tiles_list, t);
        }
    }
    mcp_compound_tag_set(level, "TileEntities", tiles_list);

    // Terrain flags.
    mcp_compound_tag_set_byte(level, "TerrainGenerated",
        chunk->terrain_populated ? 1 : 0);
    mcp_compound_tag_set_byte(level, "TerrainPopulated",
        chunk->terrain_populated ? 1 : 0);
    mcp_compound_tag_set_byte(level, "LightPopulated",
        chunk->light_populated ? 1 : 0);

    mcp_compound_tag_set(root, "Level", level);

    // Serialize big-endian.
    uint8_t* buf = mcp_nbt_write_bytes(root, /*little_endian=*/false, out_len);
    mcp_tag_destroy(root);
    return buf;
}

// =====================================================================
// DECODE
// =====================================================================
mcp_chunk_t* anvil_chunk_decode(const uint8_t* data, size_t len) {
    if (!data || len == 0) return NULL;

    mcp_tag_t* root = mcp_nbt_read_bytes(data, len, /*little_endian=*/false);
    if (!root) return NULL;
    if (mcp_tag_get_type(root) != MCP_TAG_COMPOUND) {
        mcp_tag_destroy(root);
        return NULL;
    }

    const mcp_tag_t* level = get_compound(root, "Level");
    if (!level) {
        mcp_tag_destroy(root);
        return NULL;
    }

    int32_t cx = mcp_compound_tag_get_int(level, "xPos", 0);
    int32_t cz = mcp_compound_tag_get_int(level, "zPos", 0);
    mcp_chunk_t* chunk = mcp_chunk_new(cx, cz);
    if (!chunk) {
        mcp_tag_destroy(root);
        return NULL;
    }

    chunk->last_update = mcp_compound_tag_get_long(level, "LastUpdate", 0);
    chunk->terrain_populated =
        mcp_compound_tag_get_byte(level, "TerrainPopulated", 0) != 0 ||
        mcp_compound_tag_get_byte(level, "TerrainGenerated", 0) != 0;
    chunk->light_populated =
        mcp_compound_tag_get_byte(level, "LightPopulated", 0) != 0;

    // Sections list.
    const mcp_tag_t* sections = mcp_compound_tag_get(level, "Sections");
    if (sections && mcp_tag_get_type(sections) == MCP_TAG_LIST) {
        const mcp_list_tag_t* lt = (const mcp_list_tag_t*)sections;
        for (size_t i = 0; i < lt->count; ++i) {
            const mcp_tag_t* sec = mcp_list_tag_get(sections, i);
            if (!sec || mcp_tag_get_type(sec) != MCP_TAG_COMPOUND) continue;
            int8_t y_idx = mcp_compound_tag_get_byte(sec, "Y", -1);
            if (y_idx < 0 || y_idx >= ANVIL_SECTIONS) continue;
            mcp_chunk_section_t* s = mcp_chunk_get_or_alloc_section(chunk, (uint8_t)y_idx);
            if (!s) continue;

            size_t bl_len = 0;
            const uint8_t* bl = get_byte_array(sec, "Blocks", &bl_len);
            if (bl && bl_len >= 4096 && s->blocks) {
                memcpy(s->blocks, bl, 4096);
            }

            size_t d_len = 0;
            const uint8_t* d = get_byte_array(sec, "Data", &d_len);
            if (d && d_len >= 2048) {
                if (!s->data) s->data = (uint8_t*)calloc(2048, 1);
                if (s->data) memcpy(s->data, d, 2048);
            }

            size_t bl2_len = 0;
            const uint8_t* bl2 = get_byte_array(sec, "BlockLight", &bl2_len);
            if (bl2 && bl2_len >= 2048) {
                if (!s->block_light) s->block_light = (uint8_t*)calloc(2048, 1);
                if (s->block_light) memcpy(s->block_light, bl2, 2048);
            }

            size_t sl_len = 0;
            const uint8_t* sl = get_byte_array(sec, "SkyLight", &sl_len);
            if (sl && sl_len >= 2048) {
                if (!s->sky_light) s->sky_light = (uint8_t*)calloc(2048, 1);
                if (s->sky_light) memcpy(s->sky_light, sl, 2048);
            } else {
                // Default to full bright.
                if (!s->sky_light) s->sky_light = (uint8_t*)malloc(2048);
                if (s->sky_light) memset(s->sky_light, 0xFF, 2048);
            }
        }
    }

    // BiomeColors → biomes (256 bytes).
    size_t bc_count = 0;
    const int32_t* bc = get_int_array(level, "BiomeColors", &bc_count);
    if (bc && bc_count >= 256) {
        if (!chunk->biomes) chunk->biomes = (uint8_t*)calloc(256, 1);
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
        if (!chunk->height_map) chunk->height_map = (int32_t*)calloc(256, sizeof(int32_t));
        if (chunk->height_map) {
            for (int i = 0; i < 256; ++i) {
                chunk->height_map[i] = hm[i];
            }
        }
    }

    // Tile entities.
    const mcp_tag_t* tiles_list = mcp_compound_tag_get(level, "TileEntities");
    if (tiles_list && mcp_tag_get_type(tiles_list) == MCP_TAG_LIST) {
        const mcp_list_tag_t* lt = (const mcp_list_tag_t*)tiles_list;
        for (size_t i = 0; i < lt->count; ++i) {
            const mcp_tag_t* t = mcp_list_tag_get(tiles_list, i);
            if (!t) continue;
            size_t blen = 0;
            uint8_t* bbuf = mcp_nbt_write_bytes(t, /*little_endian=*/true, &blen);
            if (bbuf && blen > 0) {
                mcp_chunk_add_tile_entity(chunk, bbuf, blen);
            }
            free(bbuf);
        }
    }

    // Entities.
    const mcp_tag_t* entities_list = mcp_compound_tag_get(level, "Entities");
    if (entities_list && mcp_tag_get_type(entities_list) == MCP_TAG_LIST) {
        const mcp_list_tag_t* lt = (const mcp_list_tag_t*)entities_list;
        for (size_t i = 0; i < lt->count; ++i) {
            const mcp_tag_t* t = mcp_list_tag_get(entities_list, i);
            if (!t) continue;
            size_t blen = 0;
            uint8_t* bbuf = mcp_nbt_write_bytes(t, /*little_endian=*/true, &blen);
            if (bbuf && blen > 0) {
                mcp_chunk_add_entity(chunk, bbuf, blen);
            }
            free(bbuf);
        }
    }

    mcp_tag_destroy(root);
    chunk->dirty = false;
    return chunk;
}
