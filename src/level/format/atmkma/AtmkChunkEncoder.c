// SPDX-License-Identifier: LGPL-3.0-or-later
// AtmkChunkEncoder.c — encodes mcp_chunk_t* to NBT little-endian
// (PMMP MCPE 0.15 format: 128-block-tall, blocks+data+skyLight+blockLight
// +heightMap+biomeColors+tiles+entities+flags).
#include "AtmkChunkEncoder.h"
#include "level/format/Chunk.h"
#include "level/format/ChunkSection.h"
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

#define SECTIONS_HIGH 8
#define BLOCKS_SIZE   32768
#define NIBBLE_SIZE   16384
#define HEIGHT_MAP_SIZE 256
#define BIOME_COLORS_SIZE 1024

static inline size_t mcpe_block_index(int x, int y, int z) {
    return (size_t)(y & 127) * 256 + (size_t)(z & 15) * 16 + (size_t)(x & 15);
}
static inline size_t mcpe_nibble_index(int x, int y, int z) {
    return mcpe_block_index(x, y, z) / 2;
}

uint8_t* atmk_chunk_encode(const mcp_chunk_t* chunk, size_t* out_len) {
    if (!chunk || !out_len) return NULL;
    *out_len = 0;

    mcp_tag_t* root = mcp_compound_tag_new_named("");
    if (!root) return NULL;

    mcp_compound_tag_set_int(root, "xPos", chunk->x);
    mcp_compound_tag_set_int(root, "zPos", chunk->z);
    mcp_compound_tag_set_long(root, "LastUpdate", chunk->last_update);
    mcp_compound_tag_set_long(root, "InhabitedTime", 0);
    mcp_compound_tag_set_byte(root, "vxl", 0);

    uint8_t* blocks     = (uint8_t*)calloc(BLOCKS_SIZE, 1);
    uint8_t* data       = (uint8_t*)calloc(NIBBLE_SIZE, 1);
    uint8_t* skyLight   = (uint8_t*)calloc(NIBBLE_SIZE, 1);
    uint8_t* blockLight = (uint8_t*)calloc(NIBBLE_SIZE, 1);
    if (!blocks || !data || !skyLight || !blockLight) {
        free(blocks); free(data); free(skyLight); free(blockLight);
        mcp_tag_destroy(root);
        return NULL;
    }

    for (int sy = 0; sy < SECTIONS_HIGH; ++sy) {
        const mcp_chunk_section_t* s = chunk->sections[sy];
        if (!s) continue;
        if (s->blocks) {
            for (int ly = 0; ly < 16; ++ly) {
                int y = sy * 16 + ly;
                for (int z = 0; z < 16; ++z) {
                    for (int x = 0; x < 16; ++x) {
                        size_t sec_i = (size_t)(ly * 256 + z * 16 + x);
                        size_t mcpe_i = mcpe_block_index(x, y, z);
                        blocks[mcpe_i] = s->blocks[sec_i];
                    }
                }
            }
        }
        if (s->data) {
            for (int ly = 0; ly < 16; ++ly) {
                int y = sy * 16 + ly;
                for (int z = 0; z < 16; ++z) {
                    for (int x = 0; x < 16; ++x) {
                        uint8_t v = mcp_section_get_nibble(s->data, x, ly, z);
                        size_t ni = mcpe_nibble_index(x, y, z);
                        if (x & 1) data[ni] = (uint8_t)((data[ni] & 0x0F) | (v << 4));
                        else       data[ni] = (uint8_t)((data[ni] & 0xF0) | (v & 0x0F));
                    }
                }
            }
        }
        if (s->sky_light) {
            for (int ly = 0; ly < 16; ++ly) {
                int y = sy * 16 + ly;
                for (int z = 0; z < 16; ++z) {
                    for (int x = 0; x < 16; ++x) {
                        uint8_t v = mcp_section_get_nibble(s->sky_light, x, ly, z);
                        size_t ni = mcpe_nibble_index(x, y, z);
                        if (x & 1) skyLight[ni] = (uint8_t)((skyLight[ni] & 0x0F) | (v << 4));
                        else       skyLight[ni] = (uint8_t)((skyLight[ni] & 0xF0) | (v & 0x0F));
                    }
                }
            }
        } else {
            for (int ly = 0; ly < 16; ++ly) {
                int y = sy * 16 + ly;
                for (int z = 0; z < 16; ++z) {
                    for (int x = 0; x < 16; ++x) {
                        size_t ni = mcpe_nibble_index(x, y, z);
                        if (x & 1) skyLight[ni] = (uint8_t)((skyLight[ni] & 0x0F) | (0x0F << 4));
                        else       skyLight[ni] = (uint8_t)((skyLight[ni] & 0xF0) | 0x0F);
                    }
                }
            }
        }
        if (s->block_light) {
            for (int ly = 0; ly < 16; ++ly) {
                int y = sy * 16 + ly;
                for (int z = 0; z < 16; ++z) {
                    for (int x = 0; x < 16; ++x) {
                        uint8_t v = mcp_section_get_nibble(s->block_light, x, ly, z);
                        size_t ni = mcpe_nibble_index(x, y, z);
                        if (x & 1) blockLight[ni] = (uint8_t)((blockLight[ni] & 0x0F) | (v << 4));
                        else       blockLight[ni] = (uint8_t)((blockLight[ni] & 0xF0) | (v & 0x0F));
                    }
                }
            }
        }
    }

    uint8_t* heightMap = (uint8_t*)calloc(HEIGHT_MAP_SIZE, 1);
    if (!heightMap) {
        free(blocks); free(data); free(skyLight); free(blockLight);
        mcp_tag_destroy(root);
        return NULL;
    }
    if (chunk->height_map) {
        for (int i = 0; i < 256; ++i) {
            int32_t v = chunk->height_map[i];
            if (v > 255) v = 255;
            if (v < 0)   v = 0;
            heightMap[i] = (uint8_t)v;
        }
    } else {
        for (int x = 0; x < 16; ++x) {
            for (int z = 0; z < 16; ++z) {
                int h = 0;
                for (int y = 127; y >= 0; --y) {
                    if (blocks[mcpe_block_index(x, y, z)] != 0) {
                        h = y + 1; break;
                    }
                }
                heightMap[z * 16 + x] = (uint8_t)h;
            }
        }
    }

    uint8_t* biomeColors = (uint8_t*)calloc(BIOME_COLORS_SIZE, 1);
    if (!biomeColors) {
        free(blocks); free(data); free(skyLight); free(blockLight); free(heightMap);
        mcp_tag_destroy(root);
        return NULL;
    }
    if (chunk->biomes) {
        for (int i = 0; i < 256; ++i) {
            biomeColors[i * 4 + 0] = chunk->biomes[i];
        }
    } else {
        for (int i = 0; i < 256; ++i) {
            biomeColors[i * 4 + 0] = 1;
        }
    }

    mcp_compound_tag_set(root, "Blocks",
        mcp_byte_array_tag_new_named("Blocks", blocks, BLOCKS_SIZE));
    mcp_compound_tag_set(root, "Data",
        mcp_byte_array_tag_new_named("Data", data, NIBBLE_SIZE));
    mcp_compound_tag_set(root, "SkyLight",
        mcp_byte_array_tag_new_named("SkyLight", skyLight, NIBBLE_SIZE));
    mcp_compound_tag_set(root, "BlockLight",
        mcp_byte_array_tag_new_named("BlockLight", blockLight, NIBBLE_SIZE));
    mcp_compound_tag_set(root, "HeightMap",
        mcp_byte_array_tag_new_named("HeightMap", heightMap, HEIGHT_MAP_SIZE));
    mcp_compound_tag_set(root, "BiomeColors",
        mcp_byte_array_tag_new_named("BiomeColors", biomeColors, BIOME_COLORS_SIZE));

    static const int32_t empty_ints[1] = {0};
    mcp_compound_tag_set(root, "ExtraData",
        mcp_int_array_tag_new_named("ExtraData", empty_ints, 0));

    mcp_tag_t* tiles_list = mcp_list_tag_new_named("Tiles", MCP_TAG_COMPOUND);
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
    mcp_compound_tag_set(root, "Tiles", tiles_list);

    mcp_tag_t* entities_list = mcp_list_tag_new_named("Entities", MCP_TAG_COMPOUND);
    for (size_t i = 0; i < chunk->entity_count; ++i) {
        if (!chunk->entities || !chunk->entity_lens) continue;
        const uint8_t* blob = chunk->entities[i];
        size_t blen = chunk->entity_lens[i];
        if (!blob || blen == 0) continue;
        mcp_tag_t* t = mcp_nbt_read_bytes(blob, blen, /*little_endian=*/true);
        if (t) {
            mcp_tag_set_name_cstr(t, "");
            mcp_list_tag_append(entities_list, t);
        }
    }
    mcp_compound_tag_set(root, "Entities", entities_list);

    mcp_compound_tag_set_byte(root, "Terrain",
        chunk->terrain_populated ? 1 : 0);
    mcp_compound_tag_set_byte(root, "TerrainPopulated",
        chunk->terrain_populated ? 1 : 0);
    mcp_compound_tag_set_byte(root, "LightPopulated",
        chunk->light_populated ? 1 : 0);

    size_t len = 0;
    uint8_t* buf = mcp_nbt_write_bytes(root, /*little_endian=*/true, &len);
    mcp_tag_destroy(root);
    *out_len = len;
    return buf;
}
