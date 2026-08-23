// SPDX-License-Identifier: LGPL-3.0-or-later
// Chunk — implementation of the pure-C chunk struct.
//
// Memory model: every section is independently allocated, so a
// freshly generated chunk costs only 1 malloc for the Chunk itself
// plus 16 lazy-faulted section_t*'s. Setting a block in a section
// that doesn't exist yet triggers an alloc+zero.
//
// Coordinate packing (XZY):
//   index = (x & 15) | ((z & 15) << 4) | ((y & 15) << 8)
// This matches Java/MCPE chunk storage, so we can memcpy a section's
// `blocks` array directly into the on-disk format without conversion.
//
// Nibble packing for data/sky_light/block_light:
//   index  = (x & 15) | ((z & 15) << 4) | ((y & 15) << 7)  // 2048 entries
//   byte   = nibbles[index >> 1]
//   low    = (byte & 0x0F) when index is even, high nibble when odd.
#include "Chunk.h"

#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------- internals
//
// section_new/section_destroy live in ChunkSection.c. Here we only
// keep the XZY/nibble helpers that operate on a section within a Chunk
// context (we don't want to expose these in ChunkSection.h since they
// are private to the storage layer).

// XZY index for the 4096-byte `blocks` array.
static inline uint32_t block_index(int x, int y, int z) {
    return (uint32_t)((x & 15) | ((z & 15) << 4) | ((y & 15) << 8));
}

// Nibble index for the 2048-byte data/sky/blocklight arrays.
static inline uint32_t nibble_index(int x, int y, int z) {
    return (uint32_t)((x & 15) | ((z & 15) << 4) | ((y & 15) << 7));
}

static inline uint8_t get_nibble(const uint8_t* arr, int x, int y, int z) {
    if (!arr) return 0;
    uint32_t i   = nibble_index(x, y, z);
    uint8_t  byte = arr[i >> 1];
    return (i & 1) ? (uint8_t)((byte >> 4) & 0x0F)
                  : (uint8_t)(byte & 0x0F);
}

static inline void set_nibble(uint8_t* arr, int x, int y, int z, uint8_t v) {
    if (!arr) return;
    v &= 0x0F;
    uint32_t i   = nibble_index(x, y, z);
    uint8_t* byte = &arr[i >> 1];
    if (i & 1) {
        *byte = (uint8_t)((*byte & 0x0F) | (v << 4));
    } else {
        *byte = (uint8_t)((*byte & 0xF0) | v);
    }
}

// ---------------------------------------------------------------- lifecycle

mcp_chunk_t* mcp_chunk_new(int32_t x, int32_t z) {
    mcp_chunk_t* c = (mcp_chunk_t*)calloc(1, sizeof(*c));
    if (!c) return NULL;
    c->x = x;
    c->z = z;
    c->last_update      = 0;
    c->terrain_populated = false;
    c->light_populated   = false;
    c->dirty             = false;
    for (int i = 0; i < 16; ++i) c->sections[i] = NULL;
    c->biomes     = (uint8_t*)calloc(256, 1);
    c->height_map = (int32_t*)calloc(256, sizeof(int32_t));
    c->tile_entities = NULL;
    c->tile_entity_lens = NULL;
    c->tile_entity_count = 0;
    c->tile_entity_cap   = 0;
    c->entities      = NULL;
    c->entity_lens   = NULL;
    c->entity_count  = 0;
    c->entity_cap    = 0;
    return c;
}

void mcp_chunk_destroy(mcp_chunk_t* c) {
    if (!c) return;
    for (int i = 0; i < 16; ++i) {
        mcp_section_destroy(c->sections[i]);
        c->sections[i] = NULL;
    }
    free(c->biomes);
    free(c->height_map);
    mcp_chunk_clear_tile_entities(c);
    mcp_chunk_clear_entities(c);
    free(c);
}

// ---------------------------------------------------------------- sections

mcp_chunk_section_t* mcp_chunk_get_section(mcp_chunk_t* c, uint8_t y) {
    if (!c || y > 15) return NULL;
    return c->sections[y];
}

mcp_chunk_section_t* mcp_chunk_get_or_alloc_section(mcp_chunk_t* c, uint8_t y) {
    if (!c || y > 15) return NULL;
    if (!c->sections[y]) {
        c->sections[y] = mcp_section_new(y);
    }
    return c->sections[y];
}

// ---------------------------------------------------------------- blocks

uint8_t mcp_chunk_get_block_id(const mcp_chunk_t* c, int x, int y, int z) {
    if (!c || y < 0 || y > 255) return 0;
    const mcp_chunk_section_t* s = c->sections[y >> 4];
    if (!s || !s->blocks) return 0;
    return s->blocks[block_index(x, y, z)];
}

uint8_t mcp_chunk_get_block_data(const mcp_chunk_t* c, int x, int y, int z) {
    if (!c || y < 0 || y > 255) return 0;
    const mcp_chunk_section_t* s = c->sections[y >> 4];
    return get_nibble(s ? s->data : NULL, x, y, z);
}

void mcp_chunk_set_block_full(mcp_chunk_t* c, int x, int y, int z,
                                uint8_t block_id, uint8_t data) {
    if (!c || y < 0 || y > 255) return;
    mcp_chunk_section_t* s = mcp_chunk_get_or_alloc_section(c, (uint8_t)(y >> 4));
    if (!s) return;
    if (s->blocks) s->blocks[block_index(x, y, z)] = block_id;
    set_nibble(s->data, x, y, z, data);
    if (block_id != 0) {
        s->non_air_top = true;
    }
    // Recompute height map lazily; bulk path does it once after edits.
    int col = (x & 15) + (z & 15) * 16;
    if (block_id != 0 && (y + 1) > c->height_map[col]) {
        c->height_map[col] = y + 1;
    }
    c->dirty = true;
}

void mcp_chunk_set_block_id(mcp_chunk_t* c, int x, int y, int z, uint8_t id) {
    if (!c || y < 0 || y > 255) return;
    mcp_chunk_section_t* s = mcp_chunk_get_or_alloc_section(c, (uint8_t)(y >> 4));
    if (!s || !s->blocks) return;
    s->blocks[block_index(x, y, z)] = id;
    if (id != 0) s->non_air_top = true;
    int col = (x & 15) + (z & 15) * 16;
    if (id != 0 && (y + 1) > c->height_map[col]) {
        c->height_map[col] = y + 1;
    }
    c->dirty = true;
}

void mcp_chunk_set_block_data(mcp_chunk_t* c, int x, int y, int z, uint8_t data) {
    if (!c || y < 0 || y > 255) return;
    mcp_chunk_section_t* s = mcp_chunk_get_or_alloc_section(c, (uint8_t)(y >> 4));
    if (!s) return;
    set_nibble(s->data, x, y, z, data);
    c->dirty = true;
}

// ---------------------------------------------------------------- lighting

uint8_t mcp_chunk_get_sky_light(const mcp_chunk_t* c, int x, int y, int z) {
    if (!c || y < 0 || y > 255) return 0;
    const mcp_chunk_section_t* s = c->sections[y >> 4];
    return get_nibble(s ? s->sky_light : NULL, x, y, z);
}

void mcp_chunk_set_sky_light(mcp_chunk_t* c, int x, int y, int z, uint8_t v) {
    if (!c || y < 0 || y > 255) return;
    mcp_chunk_section_t* s = mcp_chunk_get_or_alloc_section(c, (uint8_t)(y >> 4));
    if (!s) return;
    set_nibble(s->sky_light, x, y, z, v);
    c->dirty = true;
}

uint8_t mcp_chunk_get_block_light(const mcp_chunk_t* c, int x, int y, int z) {
    if (!c || y < 0 || y > 255) return 0;
    const mcp_chunk_section_t* s = c->sections[y >> 4];
    return get_nibble(s ? s->block_light : NULL, x, y, z);
}

void mcp_chunk_set_block_light(mcp_chunk_t* c, int x, int y, int z, uint8_t v) {
    if (!c || y < 0 || y > 255) return;
    mcp_chunk_section_t* s = mcp_chunk_get_or_alloc_section(c, (uint8_t)(y >> 4));
    if (!s) return;
    set_nibble(s->block_light, x, y, z, v);
    c->dirty = true;
}

// ---------------------------------------------------------------- biome + heightmap

uint8_t mcp_chunk_get_biome(const mcp_chunk_t* c, int x, int z) {
    if (!c || !c->biomes) return 0;
    return c->biomes[(x & 15) + (z & 15) * 16];
}

void mcp_chunk_set_biome(mcp_chunk_t* c, int x, int z, uint8_t b) {
    if (!c || !c->biomes) return;
    c->biomes[(x & 15) + (z & 15) * 16] = b;
    c->dirty = true;
}

int32_t mcp_chunk_get_height_map(const mcp_chunk_t* c, int x, int z) {
    if (!c || !c->height_map) return 0;
    return c->height_map[(x & 15) + (z & 15) * 16];
}

void mcp_chunk_set_height_map(mcp_chunk_t* c, int x, int z, int32_t h) {
    if (!c || !c->height_map) return;
    c->height_map[(x & 15) + (z & 15) * 16] = h;
    c->dirty = true;
}

void mcp_chunk_recalculate_height_map(mcp_chunk_t* c) {
    if (!c || !c->height_map) return;
    for (int col = 0; col < 256; ++col) {
        int x = col & 15;
        int z = (col >> 4) & 15;
        int32_t h = 0;
        for (int y = 255; y >= 0; --y) {
            if (mcp_chunk_get_block_id(c, x, y, z) != 0) {
                h = y + 1;
                break;
            }
        }
        c->height_map[col] = h;
    }
    c->dirty = true;
}

// ---------------------------------------------------------------- tile-entities + entities

static int push_blob(uint8_t*** arr_ptr, size_t** len_ptr,
                       size_t* count_ptr, size_t* cap_ptr,
                       const uint8_t* src, size_t len) {
    if (!arr_ptr || !len_ptr || !count_ptr || !cap_ptr) return -1;
    if (*count_ptr >= *cap_ptr) {
        size_t new_cap = (*cap_ptr == 0) ? 4 : (*cap_ptr * 2);
        uint8_t** na = (uint8_t**)realloc(*arr_ptr, new_cap * sizeof(uint8_t*));
        if (!na) return -1;
        *arr_ptr = na;
        size_t* nl = (size_t*)realloc(*len_ptr, new_cap * sizeof(size_t));
        if (!nl) return -1;
        *len_ptr = nl;
        *cap_ptr = new_cap;
    }
    uint8_t* copy = (uint8_t*)malloc(len > 0 ? len : 1);
    if (!copy) return -1;
    if (len > 0 && src) memcpy(copy, src, len);
    (*arr_ptr)[*count_ptr] = copy;
    (*len_ptr)[*count_ptr] = len;
    *count_ptr = *count_ptr + 1;
    return 0;
}

int mcp_chunk_add_tile_entity(mcp_chunk_t* c, const uint8_t* nbt, size_t len) {
    if (!c) return -1;
    int rc = push_blob(&c->tile_entities, &c->tile_entity_lens,
                       &c->tile_entity_count, &c->tile_entity_cap, nbt, len);
    if (rc == 0) c->dirty = true;
    return rc;
}

int mcp_chunk_add_entity(mcp_chunk_t* c, const uint8_t* nbt, size_t len) {
    if (!c) return -1;
    int rc = push_blob(&c->entities, &c->entity_lens,
                       &c->entity_count, &c->entity_cap, nbt, len);
    if (rc == 0) c->dirty = true;
    return rc;
}

void mcp_chunk_clear_tile_entities(mcp_chunk_t* c) {
    if (!c) return;
    if (c->tile_entities) {
        for (size_t i = 0; i < c->tile_entity_count; ++i) {
            free(c->tile_entities[i]);
        }
        free(c->tile_entities);
        c->tile_entities = NULL;
    }
    free(c->tile_entity_lens);
    c->tile_entity_lens = NULL;
    c->tile_entity_count = 0;
    c->tile_entity_cap   = 0;
}

void mcp_chunk_clear_entities(mcp_chunk_t* c) {
    if (!c) return;
    if (c->entities) {
        for (size_t i = 0; i < c->entity_count; ++i) {
            free(c->entities[i]);
        }
        free(c->entities);
        c->entities = NULL;
    }
    free(c->entity_lens);
    c->entity_lens = NULL;
    c->entity_count = 0;
    c->entity_cap   = 0;
}

// ---------------------------------------------------------------- section flags

void mcp_chunk_recompute_section_flags(mcp_chunk_t* c) {
    if (!c) return;
    for (int i = 0; i < 16; ++i) {
        mcp_chunk_section_t* s = c->sections[i];
        if (!s || !s->blocks) continue;
        bool non_air = false;
        for (int j = 2048; j < 4096; ++j) {  // upper half of section
            if (s->blocks[j] != 0) { non_air = true; break; }
        }
        s->non_air_top = non_air;
    }
}

// ---------------------------------------------------------------- network serialization

// MCPE 0.15.x FullChunkDataPacket payload format (protocol 0x34):
//   16 sections * (4096 blocks + 2048 data + 2048 sky + 2048 blocklight)
//   = 16 * 8192 = 131072 bytes
//   + 256 bytes biomes
//   + 256*4 = 1024 bytes heightmap (big-endian int32)
//   + 1 byte extra (0x00 — historical "premium" flag)
// Total: 132353 bytes typical.
uint8_t* mcp_chunk_network_serialize(const mcp_chunk_t* c, size_t* out_len) {
    if (!c || !out_len) return NULL;

    // MCPE 0.15.10 ORDER_COLUMNS format (128-height world):
    //   32768 bytes: block IDs   (index = x<<11 | z<<7 | y, y=0..127)
    //   16384 bytes: block data  (nibbles)
    //   16384 bytes: sky light  (nibbles)
    //   16384 bytes: block light (nibbles)
    //   256 bytes:  heightmap   (one byte per column)
    //   1024 bytes: biome colors (int32 BE per column: R<<24|G<<16|B<<8|0)
    //   4 bytes:    extra data count (LInt32 = 0)
    // Total: 66036 bytes

    const size_t kBlocks    = 32768;  // 16*16*128
    const size_t kData      = 16384;  // 16*16*64
    const size_t kSkyLight  = 16384;
    const size_t kBlockLight= 16384;
    const size_t kHeightMap= 256;
    const size_t kBiomeCol  = 1024;   // 256 * 4
    const size_t kExtra     = 4;      // LInt32 count = 0
    size_t total = kBlocks + kData + kSkyLight + kBlockLight + kHeightMap + kBiomeCol + kExtra;
    uint8_t* buf = (uint8_t*)calloc(total, 1);
    if (!buf) return NULL;

    size_t off = 0;

    // --- Block IDs: convert from sections (XZY, 256-height) to columns (128-height) ---
    // PMMP column index: (x << 11) | (z << 7) | y, where y = 0..127
    // Our section index: section[y>>4].blocks[(x&15) | ((z&15)<<4) | ((y&15)<<8)]
    for (int x = 0; x < 16; ++x) {
        for (int z = 0; z < 16; ++z) {
            for (int y = 0; y < 128; ++y) {
                uint8_t id = 0;
                const mcp_chunk_section_t* s = c->sections[y >> 4];
                if (s && s->blocks) {
                    uint32_t sec_idx = (uint32_t)((x & 15) | ((z & 15) << 4) | ((y & 15) << 8));
                    id = s->blocks[sec_idx];
                }
                buf[(x << 11) | (z << 7) | y] = id;
            }
        }
    }
    off += kBlocks;

    // --- Block data (nibbles): same column order ---
    for (int x = 0; x < 16; ++x) {
        for (int z = 0; z < 16; ++z) {
            for (int y = 0; y < 128; y += 2) {
                uint8_t lo = 0, hi = 0;
                const mcp_chunk_section_t* s = c->sections[y >> 4];
                if (s && s->data) {
                    uint32_t ni = (uint32_t)((x & 15) | ((z & 15) << 4) | ((y & 15) << 7));
                    lo = (s->data[ni >> 1] >> ((ni & 1) << 2)) & 0x0F;
                }
                {
                    int y2 = y + 1;
                    const mcp_chunk_section_t* s2 = c->sections[y2 >> 4];
                    if (s2 && s2->data) {
                        uint32_t ni = (uint32_t)((x & 15) | ((z & 15) << 4) | ((y2 & 15) << 7));
                        hi = (s2->data[ni >> 1] >> ((ni & 1) << 2)) & 0x0F;
                    }
                }
                uint32_t ci = (uint32_t)((x << 11) | (z << 7) | y);
                buf[kBlocks + (ci >> 1)] = (hi << 4) | lo;
            }
        }
    }
    off += kData;

    // --- Sky light: fill with 0xFF (full bright) ---
    memset(buf + off, 0xFF, kSkyLight);
    off += kSkyLight;

    // --- Block light: all zeros ---
    off += kBlockLight;  // already zero from calloc

    // --- Heightmap: one byte per column (max y with non-air block, capped at 127) ---
    if (c->height_map) {
        for (int x = 0; x < 16; ++x) {
            for (int z = 0; z < 16; ++z) {
                int32_t h = c->height_map[(x & 15) + (z & 15) * 16];
                if (h > 127) h = 127;
                buf[off + (x & 15) + (z & 15) * 16] = (uint8_t)h;
            }
        }
    }
    off += kHeightMap;

    // --- Biome colors: int32 BE per column (R=0x00, G=0x80, B=0x40, A=0x00 = plains green) ---
    for (int i = 0; i < 256; ++i) {
        buf[off + 0] = 0x00;  // R
        buf[off + 1] = 0x80;  // G  
        buf[off + 2] = 0x40;  // B
        buf[off + 3] = 0x00;  // unused
        off += 4;
    }

    // --- Extra data: LInt32 count = 0 ---
    buf[off + 0] = 0;
    buf[off + 1] = 0;
    buf[off + 2] = 0;
    buf[off + 3] = 0;
    off += 4;

    *out_len = off;
    return buf;
}
