// SPDX-License-Identifier: LGPL-3.0-or-later
// AtmkChunkDecoder.c — decodes NBT little-endian back into mcp_chunk_t*.
#include "AtmkChunkDecoder.h"
#include "level/format/Chunk.h"
#include "level/format/ChunkSection.h"
#include "nbt/NBT.h"
#include "nbt/tag/CompoundTag.h"
#include "nbt/tag/ByteArrayTag.h"
#include "nbt/tag/IntArrayTag.h"
#include "nbt/tag/ListTag.h"
#include "nbt/tag/Tag.h"
#include <stdlib.h>
#include <string.h>

#define SECTIONS_HIGH 8

static inline size_t mcpe_block_index(int x, int y, int z) {
    return (size_t)(y & 127) * 256 + (size_t)(z & 15) * 16 + (size_t)(x & 15);
}
static inline size_t mcpe_nibble_index(int x, int y, int z) {
    return mcpe_block_index(x, y, z) / 2;
}

static const uint8_t* get_byte_array(const mcp_tag_t* root, const char* name,
                                        size_t* out_len) {
    const mcp_tag_t* t = mcp_compound_tag_get(root, name);
    if (!t) return NULL;
    if (mcp_tag_get_type(t) != MCP_TAG_BYTE_ARRAY) return NULL;
    const mcp_byte_array_tag_t* ba = (const mcp_byte_array_tag_t*)t;
    if (out_len) *out_len = ba->length;
    return ba->data;
}

static void apply_nibble_array(mcp_chunk_t* chunk, const uint8_t* src,
                                  size_t src_len, int kind) {
    if (!chunk || !src || src_len < 16384) return;
    for (int sy = 0; sy < SECTIONS_HIGH; ++sy) {
        mcp_chunk_section_t* s = mcp_chunk_get_or_alloc_section(chunk, (uint8_t)sy);
        if (!s) continue;
        uint8_t* arr = NULL;
        switch (kind) {
            case 0: arr = s->data;        break;
            case 1: arr = s->sky_light;   break;
            case 2: arr = s->block_light; break;
        }
        if (!arr) {
            arr = (uint8_t*)calloc(2048, 1);
            if (!arr) continue;
            switch (kind) {
                case 0: s->data         = arr; break;
                case 1: s->sky_light    = arr; break;
                case 2: s->block_light  = arr; break;
            }
        }
        for (int ly = 0; ly < 16; ++ly) {
            int y = sy * 16 + ly;
            for (int z = 0; z < 16; ++z) {
                for (int x = 0; x < 16; ++x) {
                    size_t ni = mcpe_nibble_index(x, y, z);
                    if (ni >= src_len) continue;
                    uint8_t b = src[ni];
                    uint8_t v = (x & 1) ? (uint8_t)((b >> 4) & 0x0F)
                                          : (uint8_t)(b & 0x0F);
                    mcp_section_set_nibble(arr, x, ly, z, v);
                }
            }
        }
    }
}

mcp_chunk_t* atmk_chunk_decode(const uint8_t* data, size_t len) {
    if (!data || len == 0) return NULL;

    mcp_tag_t* root = mcp_nbt_read_bytes(data, len, /*little_endian=*/true);
    if (!root) return NULL;
    if (mcp_tag_get_type(root) != MCP_TAG_COMPOUND) {
        mcp_tag_destroy(root);
        return NULL;
    }

    int32_t cx = mcp_compound_tag_get_int(root, "xPos", 0);
    int32_t cz = mcp_compound_tag_get_int(root, "zPos", 0);
    mcp_chunk_t* chunk = mcp_chunk_new(cx, cz);
    if (!chunk) {
        mcp_tag_destroy(root);
        return NULL;
    }

    chunk->last_update = mcp_compound_tag_get_long(root, "LastUpdate", 0);
    chunk->terrain_populated = mcp_compound_tag_get_byte(root, "TerrainPopulated", 0) != 0;
    chunk->light_populated   = mcp_compound_tag_get_byte(root, "LightPopulated", 0) != 0;

    size_t blocks_len = 0;
    const uint8_t* blocks = get_byte_array(root, "Blocks", &blocks_len);
    if (blocks && blocks_len >= 32768) {
        for (int sy = 0; sy < SECTIONS_HIGH; ++sy) {
            mcp_chunk_section_t* s = mcp_chunk_get_or_alloc_section(chunk, (uint8_t)sy);
            if (!s || !s->blocks) continue;
            for (int ly = 0; ly < 16; ++ly) {
                int y = sy * 16 + ly;
                for (int z = 0; z < 16; ++z) {
                    for (int x = 0; x < 16; ++x) {
                        size_t sec_i = (size_t)(ly * 256 + z * 16 + x);
                        size_t mcpe_i = mcpe_block_index(x, y, z);
                        if (mcpe_i < blocks_len) {
                            s->blocks[sec_i] = blocks[mcpe_i];
                        }
                    }
                }
            }
        }
    }

    size_t na_len = 0;
    const uint8_t* na = get_byte_array(root, "Data", &na_len);
    if (na) apply_nibble_array(chunk, na, na_len, 0);
    na = get_byte_array(root, "SkyLight", &na_len);
    if (na) apply_nibble_array(chunk, na, na_len, 1);
    na = get_byte_array(root, "BlockLight", &na_len);
    if (na) apply_nibble_array(chunk, na, na_len, 2);

    size_t hm_len = 0;
    const uint8_t* hm = get_byte_array(root, "HeightMap", &hm_len);
    if (hm && hm_len >= 256) {
        if (!chunk->height_map) {
            chunk->height_map = (int32_t*)calloc(256, sizeof(int32_t));
        }
        if (chunk->height_map) {
            for (int i = 0; i < 256; ++i) {
                chunk->height_map[i] = (int32_t)hm[i];
            }
        }
    }

    size_t bc_len = 0;
    const uint8_t* bc = get_byte_array(root, "BiomeColors", &bc_len);
    if (bc && bc_len >= 1024) {
        if (!chunk->biomes) {
            chunk->biomes = (uint8_t*)calloc(256, 1);
        }
        if (chunk->biomes) {
            for (int i = 0; i < 256; ++i) {
                chunk->biomes[i] = bc[i * 4 + 0];
            }
        }
    }

    const mcp_tag_t* tiles_list = mcp_compound_tag_get(root, "Tiles");
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

    const mcp_tag_t* entities_list = mcp_compound_tag_get(root, "Entities");
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
