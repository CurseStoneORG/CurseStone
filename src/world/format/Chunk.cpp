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
//   index  = (x & 15) | ((z & 15) << 4) | ((y & 15) << 8)  // 4096 entries, 2048 bytes
//   byte   = nibbles[index >> 1]
//   low    = (byte & 0x0F) when index is even, high nibble when odd.
#include "Chunk.h"
#include "../../block/Block.h"

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

// Nibble index for the 2048-byte data/sky/blocklight arrays (4096
// nibbles packed 2-per-byte).
//
// REAL BUG (corrupted chunks/light): `y` used to shift only 7 bits.
// With z in bits 4-7, a y-shift of 7 made the top z bit (z>=8) and the
// lowest y&15 bit (odd y within the section) collide on the same index
// bit, so (x, z=0, y) and (x, z=8, y) with odd y&15 mapped to the same
// physical byte and silently overwrote each other. Visible as corrupt
// sky light and black stripes aligned with z=8. block_index() above
// already packs 4 clean bits per axis; nibble_index just needed the
// same scheme.
static inline uint32_t nibble_index(int x, int y, int z) {
    return (uint32_t)((x & 15) | ((z & 15) << 4) | ((y & 15) << 8));
}

static inline uint8_t get_nibble(const uint8_t* arr, int x, int y, int z) {
    if (!arr)
        return 0;
    uint32_t i = nibble_index(x, y, z);
    uint8_t byte = arr[i >> 1];
    return (i & 1) ? (uint8_t)((byte >> 4) & 0x0F) : (uint8_t)(byte & 0x0F);
}

static inline void set_nibble(uint8_t* arr, int x, int y, int z, uint8_t v) {
    if (!arr)
        return;
    v &= 0x0F;
    uint32_t i = nibble_index(x, y, z);
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
    if (!c)
        return NULL;
    c->x = x;
    c->z = z;
    c->last_update = 0;
    c->terrain_populated = false;
    c->light_populated = false;
    c->dirty = false;
    for (int i = 0; i < 16; ++i)
        c->sections[i] = NULL;
    c->biomes = (uint8_t*)calloc(256, 1);
    c->height_map = (int32_t*)calloc(256, sizeof(int32_t));
    c->tile_entities = NULL;
    c->tile_entity_lens = NULL;
    c->tile_entity_count = 0;
    c->tile_entity_cap = 0;
    c->entities = NULL;
    c->entity_lens = NULL;
    c->entity_count = 0;
    c->entity_cap = 0;
    return c;
}

void mcp_chunk_destroy(mcp_chunk_t* c) {
    if (!c)
        return;
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
    if (!c || y > 15)
        return NULL;
    return c->sections[y];
}

mcp_chunk_section_t* mcp_chunk_get_or_alloc_section(mcp_chunk_t* c, uint8_t y) {
    if (!c || y > 15)
        return NULL;
    if (!c->sections[y]) {
        c->sections[y] = mcp_section_new(y);
    }
    return c->sections[y];
}

// ---------------------------------------------------------------- blocks

uint8_t mcp_chunk_get_block_id(const mcp_chunk_t* c, int x, int y, int z) {
    if (!c || y < 0 || y > 255)
        return 0;
    const mcp_chunk_section_t* s = c->sections[y >> 4];
    if (!s || !s->blocks)
        return 0;
    return s->blocks[block_index(x, y, z)];
}

uint8_t mcp_chunk_get_block_data(const mcp_chunk_t* c, int x, int y, int z) {
    if (!c || y < 0 || y > 255)
        return 0;
    const mcp_chunk_section_t* s = c->sections[y >> 4];
    return get_nibble(s ? s->data : NULL, x, y, z);
}

void mcp_chunk_set_block_full(mcp_chunk_t* c, int x, int y, int z, uint8_t block_id, uint8_t data) {
    if (!c || y < 0 || y > 255)
        return;
    mcp_chunk_section_t* s = mcp_chunk_get_or_alloc_section(c, (uint8_t)(y >> 4));
    if (!s)
        return;
    if (s->blocks)
        s->blocks[block_index(x, y, z)] = block_id;
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
    if (!c || y < 0 || y > 255)
        return;
    mcp_chunk_section_t* s = mcp_chunk_get_or_alloc_section(c, (uint8_t)(y >> 4));
    if (!s || !s->blocks)
        return;
    s->blocks[block_index(x, y, z)] = id;
    if (id != 0)
        s->non_air_top = true;
    int col = (x & 15) + (z & 15) * 16;
    if (id != 0 && (y + 1) > c->height_map[col]) {
        c->height_map[col] = y + 1;
    }
    c->dirty = true;
}


// ---------------------------------------------------------------- lighting

uint8_t mcp_chunk_get_sky_light(const mcp_chunk_t* c, int x, int y, int z) {
    if (!c || y < 0 || y > 255)
        return 0;
    const mcp_chunk_section_t* s = c->sections[y >> 4];
    if (s)
        return get_nibble(s->sky_light, x, y, z);
    // Un unallocated section only happens for pure air above the
    // chunk's highest surface: any section holding a real block was
    // allocated when the block was placed, and the light pass allocates
    // every section in [0, maxTop]. So "NULL section" always means open
    // sky — full brightness.
    return 15;
}

void mcp_chunk_set_sky_light(mcp_chunk_t* c, int x, int y, int z, uint8_t v) {
    if (!c || y < 0 || y > 255)
        return;
    mcp_chunk_section_t* s = mcp_chunk_get_or_alloc_section(c, (uint8_t)(y >> 4));
    if (!s)
        return;
    set_nibble(s->sky_light, x, y, z, v);
    c->dirty = true;
}

uint8_t mcp_chunk_get_block_light(const mcp_chunk_t* c, int x, int y, int z) {
    if (!c || y < 0 || y > 255)
        return 0;
    const mcp_chunk_section_t* s = c->sections[y >> 4];
    return get_nibble(s ? s->block_light : NULL, x, y, z);
}

void mcp_chunk_set_block_light(mcp_chunk_t* c, int x, int y, int z, uint8_t v) {
    if (!c || y < 0 || y > 255)
        return;
    mcp_chunk_section_t* s = mcp_chunk_get_or_alloc_section(c, (uint8_t)(y >> 4));
    if (!s)
        return;
    set_nibble(s->block_light, x, y, z, v);
    c->dirty = true;
}

// ---------------------------------------------------------------- biome + heightmap

uint8_t mcp_chunk_get_biome(const mcp_chunk_t* c, int x, int z) {
    if (!c || !c->biomes)
        return 0;
    return c->biomes[(x & 15) + (z & 15) * 16];
}

void mcp_chunk_set_biome(mcp_chunk_t* c, int x, int z, uint8_t b) {
    if (!c || !c->biomes)
        return;
    c->biomes[(x & 15) + (z & 15) * 16] = b;
    c->dirty = true;
}

int32_t mcp_chunk_get_height_map(const mcp_chunk_t* c, int x, int z) {
    if (!c || !c->height_map)
        return 0;
    return c->height_map[(x & 15) + (z & 15) * 16];
}

void mcp_chunk_set_height_map(mcp_chunk_t* c, int x, int z, int32_t h) {
    if (!c || !c->height_map)
        return;
    c->height_map[(x & 15) + (z & 15) * 16] = h;
    c->dirty = true;
}

void mcp_chunk_recalculate_height_map_column(mcp_chunk_t* c, int x, int z) {
    if (!c || !c->height_map)
        return;
    x &= 15;
    z &= 15;
    const int col = x + z * 16;
    int32_t h = 0;
    for (int y = 255; y >= 0; --y) {
        uint8_t id = mcp_chunk_get_block_id(c, x, y, z);
        if (id != 0 && cursestone::block::Block::lightOpacity(id) > 0) {
            h = y + 1;
            break;
        }
    }
    c->height_map[col] = h;
    c->dirty = true;
}

void mcp_chunk_recalculate_height_map(mcp_chunk_t* c) {
    if (!c || !c->height_map)
        return;
    // REAL BUG (black grass/world): every non-air block used to count
    // as a "ceiling", including leaves, flowers, tall grass and signs,
    // which do NOT block sky light in Minecraft. The inflated heightmap
    // made the sky light pass treat near-open columns as roofed, so
    // grass rendered black near any tree.
    //
    // Fix: only blocks with light opacity > 0 (like vanilla's
    // Block::getLightFilter() > 0) count as a ceiling now.
    for (int col = 0; col < 256; ++col)
        mcp_chunk_recalculate_height_map_column(c, col & 15, (col >> 4) & 15);
}

// ---------------------------------------------------------------- tile-entities + entities

static int push_blob(uint8_t*** arr_ptr, size_t** len_ptr, size_t* count_ptr, size_t* cap_ptr,
                     const uint8_t* src, size_t len) {
    if (!arr_ptr || !len_ptr || !count_ptr || !cap_ptr)
        return -1;
    if (*count_ptr >= *cap_ptr) {
        size_t new_cap = (*cap_ptr == 0) ? 4 : (*cap_ptr * 2);
        uint8_t** na = (uint8_t**)realloc(*arr_ptr, new_cap * sizeof(uint8_t*));
        if (!na)
            return -1;
        *arr_ptr = na;
        size_t* nl = (size_t*)realloc(*len_ptr, new_cap * sizeof(size_t));
        if (!nl)
            return -1;
        *len_ptr = nl;
        *cap_ptr = new_cap;
    }
    uint8_t* copy = (uint8_t*)malloc(len > 0 ? len : 1);
    if (!copy)
        return -1;
    if (len > 0 && src)
        memcpy(copy, src, len);
    (*arr_ptr)[*count_ptr] = copy;
    (*len_ptr)[*count_ptr] = len;
    *count_ptr = *count_ptr + 1;
    return 0;
}

int mcp_chunk_add_tile_entity(mcp_chunk_t* c, const uint8_t* nbt, size_t len) {
    if (!c)
        return -1;
    int rc = push_blob(&c->tile_entities, &c->tile_entity_lens, &c->tile_entity_count,
                       &c->tile_entity_cap, nbt, len);
    if (rc == 0)
        c->dirty = true;
    return rc;
}

int mcp_chunk_add_entity(mcp_chunk_t* c, const uint8_t* nbt, size_t len) {
    if (!c)
        return -1;
    int rc = push_blob(&c->entities, &c->entity_lens, &c->entity_count, &c->entity_cap, nbt, len);
    if (rc == 0)
        c->dirty = true;
    return rc;
}

void mcp_chunk_clear_tile_entities(mcp_chunk_t* c) {
    if (!c)
        return;
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
    c->tile_entity_cap = 0;
}

void mcp_chunk_clear_entities(mcp_chunk_t* c) {
    if (!c)
        return;
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
    c->entity_cap = 0;
}

// ---------------------------------------------------------------- section flags

void mcp_chunk_recompute_section_flags(mcp_chunk_t* c) {
    if (!c)
        return;
    for (int i = 0; i < 16; ++i) {
        mcp_chunk_section_t* s = c->sections[i];
        if (!s || !s->blocks)
            continue;
        bool non_air = false;
        for (int j = 2048; j < 4096; ++j) { // upper half of section
            if (s->blocks[j] != 0) {
                non_air = true;
                break;
            }
        }
        s->non_air_top = non_air;
    }
}

// ---------------------------------------------------------------- sky light propagation
//
// REAL BUG (black world/grass): no generator computed real light —
// they memset sky_light to 0xFF per section and set light_populated.
// MCPE 0.15.x clients do not recompute occlusion from sky_light; they
// render exactly what the server sends, so full-brightness sections
// plus neighbor combinations rendered solid black.
//
// Fix: pase real de dos fases, mismo enfoque que
// ------------------------------------------------------------- light pass
//
// This is the pass that fills a freshly generated chunk, i.e. the light
// the CLIENT receives in the very first FullChunkDataPacket for it. It
// used to flood only the four HORIZONTAL neighbours and never touched
// block light at all, so a chunk shipped to the client with black under
// every overhang and no torch light anywhere — and the client keeps that
// initial state until a block changes near it.
//
// Both layers now flood in the six vanilla directions and both are
// seeded properly. Cross-chunk continuity is still World's job (see
// LightEngine); this pass has to be right on its own so the first packet
// a player receives is already sane.

static uint8_t layer_get(const mcp_chunk_t* c, int x, int y, int z, int isSky) {
    return isSky ? mcp_chunk_get_sky_light(c, x, y, z) : mcp_chunk_get_block_light(c, x, y, z);
}

static void layer_set(mcp_chunk_t* c, int x, int y, int z, uint8_t v, int isSky) {
    if (isSky)
        mcp_chunk_set_sky_light(c, x, y, z, v);
    else
        mcp_chunk_set_block_light(c, x, y, z, v);
}

// A step into `id` costs 1 for anything light passes through and 15 for
// an opaque block, which blocks outright.
static int light_cost(uint8_t id) {
    if (id == 0)
        return 1;
    const uint8_t op = cursestone::block::Block::lightOpacity(id);
    return op > 0 ? (int)op : 1;
}

// Flood one layer through the chunk from every seeded cell, in the six
// vanilla directions. Bounded to [0, maxTop]: above that there is not a
// single block in any column of this chunk.
static void propagate_layer(mcp_chunk_t* c, int maxTop, int isSky) {
    typedef struct {
        uint8_t x, z;
        uint16_t y;
    } LightNode;
    const int dx[6] = {1, -1, 0, 0, 0, 0};
    const int dy[6] = {0, 0, 1, -1, 0, 0};
    const int dz[6] = {0, 0, 0, 0, 1, -1};

    size_t cap = (size_t)16 * 16 * (size_t)(maxTop + 1);
    if (cap == 0)
        cap = 1;
    LightNode* queue = (LightNode*)malloc(cap * sizeof(LightNode));
    if (!queue)
        return;

    size_t qhead = 0, qtail = 0;
    for (int x = 0; x < 16; ++x)
        for (int z = 0; z < 16; ++z)
            for (int y = 0; y <= maxTop; ++y)
                if (layer_get(c, x, y, z, isSky) > 1) {
                    if (qtail < cap)
                        queue[qtail++] = (LightNode){(uint8_t)x, (uint8_t)z, (uint16_t)y};
                }

    while (qhead < qtail) {
        const LightNode n = queue[qhead++];
        const uint8_t cur = layer_get(c, n.x, n.y, n.z, isSky);
        if (cur <= 1)
            continue;
        for (int d = 0; d < 6; ++d) {
            const int nx = (int)n.x + dx[d];
            const int ny = (int)n.y + dy[d];
            const int nz = (int)n.z + dz[d];
            if (ny < 0 || ny > maxTop)
                continue;
            // single-chunk pass: no cruzamos a chunks vecinos (World's
            // LightEngine se encarga del cruce de fronteras).
            if (nx < 0 || nx > 15 || nz < 0 || nz > 15)
                continue;
            const int cand = (int)cur - light_cost(mcp_chunk_get_block_id(c, nx, ny, nz));
            if (cand <= 0)
                continue;
            if ((uint8_t)cand > layer_get(c, nx, ny, nz, isSky)) {
                layer_set(c, nx, ny, nz, (uint8_t)cand, isSky);
                if (cand > 1 && qtail < cap)
                    queue[qtail++] = (LightNode){(uint8_t)nx, (uint8_t)nz, (uint16_t)ny};
            }
        }
    }
    free(queue);
}

void mcp_chunk_propagate_sky_light(mcp_chunk_t* c) {
    if (!c)
        return;

    // REAL BUG (chunks that never render): scanning y=255..0 for all
    // 256 columns allocated all 16 sections of EVERY chunk (even empty
    // sky-only ones) just to rewrite the default 15 — expensive enough
    // under concurrent generation that some chunks arrived late and
    // rendered as black holes.
    //
    // Fix: all three phases are bounded to [0, maxTop] (the tallest
    // column of the whole chunk, with margin for side shadows). Nothing
    // exists above maxTop and unallocated sections already read 15.
    int32_t maxTop = 0;
    for (int col = 0; col < 256; ++col) {
        int32_t h = c->height_map ? c->height_map[col] : 0;
        if (h > maxTop)
            maxTop = h;
    }
    if (maxTop > 255)
        maxTop = 255;

    // --- Fase 1: columna descendente (solo hasta maxTop) ---
    for (int x = 0; x < 16; ++x) {
        for (int z = 0; z < 16; ++z) {
            int32_t h = mcp_chunk_get_height_map(c, x, z); // primer Y opaco
            int light = 15;
            for (int y = maxTop; y >= 0; --y) {
                if (y >= h) {
                    // por encima de la superficie: cielo abierto, luz plena
                    mcp_chunk_set_sky_light(c, x, y, z, 15);
                    continue;
                }
                uint8_t id = mcp_chunk_get_block_id(c, x, y, z);
                uint8_t op = cursestone::block::Block::lightOpacity(id);
                // Minecraft subtracts opacity (min 1 so light still
                // decays through "transparent" blocks) except pure air.
                if (id != 0) {
                    int dec = op > 0 ? op : 1;
                    light -= dec;
                    if (light < 0)
                        light = 0;
                }
                mcp_chunk_set_sky_light(c, x, y, z, (uint8_t)light);
                if (light <= 0) {
                    // no sky light can exist lower in this column from
                    // the vertical phase; phase 2 fills from the sides.
                }
            }
        }
    }

    // --- Fase 2: inundado en las 6 direcciones, para las DOS capas ---
    // La vertical es lo que faltaba antes: sin ella la luz no bajaba
    // nunca y todo lo que estuviera bajo un alero o en una cueva
    // quedaba en 0 (las "manchas negras").
    propagate_layer(c, maxTop, /*isSky=*/1);

    // Block light: se siembra desde la emision de cada bloque y se
    // inunda igual que el cielo. Antes esta capa no se tocaba nunca,
    // asi que una antorcha no iluminaba nada.
    for (int x = 0; x < 16; ++x)
        for (int z = 0; z < 16; ++z)
            for (int y = 0; y <= maxTop; ++y) {
                const uint8_t e = cursestone::block::Block::lightEmission(mcp_chunk_get_block_id(c, x, y, z));
                mcp_chunk_set_block_light(c, x, y, z, e);
            }
    propagate_layer(c, maxTop, /*isSky=*/0);

    c->light_populated = true;
    c->dirty = true;
}

// ---------------------------------------------------------------- network serialization

// Per-biome "grasscolor" for the biome color array of the
// FullChunkDataPacket (protocol 84 / MCPE 0.15.x).
//
// The switch uses the vanilla biome ids the vgen generator writes into
// the chunk's biome byte (same ids as Java: 3=EXTREME_HILLS, 4=FOREST,
// 5=TAIGA, 6=SWAMPLAND, 7=RIVER, 16=BEACH...). The classic grasscolor
// table (0=ocean, 1=plains, 2=desert, 3=forest, 4=mountains...) does
// NOT match — cases 3/4 were swapped, tinting stone-mountains grass
// with forest green (the "unnatural grass patches on stone" bug).
//
// Every biome now has its own tone grouped by climate: cold = pale
// bluish green, taiga = gray conifer green, swamp = dark olive,
// savanna/desert = dry yellowish green, jungle = deep saturated green,
// plateau/hell = brown. Unknown biome -> plains.
static void mcp_biome_grass_color(uint8_t biomeId, uint8_t* r, uint8_t* g, uint8_t* b) {
    switch (biomeId) {
    // --- Templado ---
    case 0:
        *r = 0x8E;
        *g = 0xB9;
        *b = 0x71;
        break; // OCEAN
    case 1:
        *r = 0x91;
        *g = 0xBD;
        *b = 0x59;
        break; // PLAINS
    // NOTE (CurseStone): the chunk biome byte uses vanilla ids (vgen):
    // 3 = EXTREME_HILLS, 4 = FOREST. These two cases used to be swapped,
    // which tinted stone-mountain grass like forest.
    case 3:
        *r = 0x8A;
        *g = 0xB6;
        *b = 0x89;
        break; // EXTREME_HILLS (verde grisaceo de montana)
    case 4:
        *r = 0x79;
        *g = 0xC0;
        *b = 0x5A;
        break; // FOREST
    case 7:
        *r = 0x8E;
        *g = 0xB9;
        *b = 0x71;
        break; // RIVER
    case 16:
        *r = 0x91;
        *g = 0xBD;
        *b = 0x59;
        break; // BEACH (igual a plains)
    case 18:
        *r = 0x79;
        *g = 0xC0;
        *b = 0x5A;
        break; // FOREST_HILLS
    case 20:
        *r = 0x8A;
        *g = 0xB6;
        *b = 0x89;
        break; // EXTREME_HILLS_EDGE (id 20 vanilla)
    case 129:
        *r = 0x91;
        *g = 0xBD;
        *b = 0x59;
        break; // PLAINS_PLATEAU

    // --- Frio: verde palido azulado ---
    case 5:
        *r = 0x86;
        *g = 0xB7;
        *b = 0x83;
        break; // TAIGA
    case 10:
        *r = 0x80;
        *g = 0xB4;
        *b = 0x97;
        break; // FROZEN_OCEAN
    case 11:
        *r = 0x80;
        *g = 0xB4;
        *b = 0x97;
        break; // FROZEN_RIVER
    case 12:
        *r = 0x80;
        *g = 0xB4;
        *b = 0x97;
        break; // ICE_PLAINS
    case 13:
        *r = 0x80;
        *g = 0xB4;
        *b = 0x97;
        break; // ICE_MOUNTAINS
    case 19:
        *r = 0x86;
        *g = 0xB7;
        *b = 0x83;
        break; // TAIGA_HILLS

    // --- Humedo: verde oscuro ---
    case 6:
        *r = 0x6A;
        *g = 0x70;
        *b = 0x39;
        break; // SWAMPLAND (oliva, como en Java)
    case 21:
        *r = 0x59;
        *g = 0xC9;
        *b = 0x3C;
        break; // JUNGLE
    case 22:
        *r = 0x59;
        *g = 0xC9;
        *b = 0x3C;
        break; // JUNGLE_HILLS
    case 14:
        *r = 0x55;
        *g = 0xC9;
        *b = 0x3F;
        break; // MUSHROOM
    case 15:
        *r = 0x55;
        *g = 0xC9;
        *b = 0x3F;
        break; // MUSHROOM_ISLAND

    // --- Calido/seco: verde amarillento ---
    case 2:
        *r = 0xBF;
        *g = 0xB7;
        *b = 0x55;
        break; // DESERT
    case 17:
        *r = 0xBF;
        *g = 0xB7;
        *b = 0x55;
        break; // DESERT_HILLS
    case 35:
        *r = 0xBF;
        *g = 0xB7;
        *b = 0x55;
        break; // SAVANNA
    case 36:
        *r = 0xBD;
        *g = 0xB2;
        *b = 0x5F;
        break; // SAVANNA_PLATEAU
    case 37:
        *r = 0x90;
        *g = 0x81;
        *b = 0x4D;
        break; // MESA

    // --- No terrestre ---
    case 8:
        *r = 0x90;
        *g = 0x59;
        *b = 0x57;
        break; // HELL
    case 9:
        *r = 0x8E;
        *g = 0xB9;
        *b = 0x71;
        break; // SKY

    default:
        *r = 0x91;
        *g = 0xBD;
        *b = 0x59;
        break; // desconocido -> plains
    }
}

// MCPE 0.15.x FullChunkDataPacket payload format (protocol 0x34):
//   16 sections * (4096 blocks + 2048 data + 2048 sky + 2048 blocklight)
//   = 16 * 8192 = 131072 bytes
//   + 256 bytes biomes
//   + 256*4 = 1024 bytes heightmap (big-endian int32)
//   + 1 byte extra (0x00 — historical "premium" flag)
// Total: 132353 bytes typical.
uint8_t* mcp_chunk_network_serialize(const mcp_chunk_t* c, size_t* out_len) {
    if (!c || !out_len)
        return NULL;

    // MCPE 0.15.10 ORDER_COLUMNS format (128-height world):
    //   32768 bytes: block IDs   (index = x<<11 | z<<7 | y, y=0..127)
    //   16384 bytes: block data  (nibbles)
    //   16384 bytes: sky light  (nibbles)
    //   16384 bytes: block light (nibbles)
    //   256 bytes:  heightmap   (one byte per column)
    //   1024 bytes: biome colors (int32 BE per column: R<<24|G<<16|B<<8|0)
    //   4 bytes:    extra data count (LInt32 = 0)
    // Total: 66036 bytes

    const size_t kBlocks = 32768; // 16*16*128
    const size_t kData = 16384;   // 16*16*64
    const size_t kSkyLight = 16384;
    const size_t kBlockLight = 16384;
    const size_t kHeightMap = 256;
    const size_t kBiomeCol = 1024; // 256 * 4
    const size_t kExtra = 4;       // LInt32 count = 0
    size_t total = kBlocks + kData + kSkyLight + kBlockLight + kHeightMap + kBiomeCol + kExtra;
    uint8_t* buf = (uint8_t*)calloc(total, 1);
    if (!buf)
        return NULL;

    size_t off = 0;

    // --- Block IDs: convert from sections (XZY, 256-height) to columns (128-height) ---
    // The reference server column index: (x << 11) | (z << 7) | y, where y = 0..127
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
                    // Mismo empaquetado que nibble_index() arriba — shift
                    // de 8, no de 7 (ver el fix de nibble_index).
                    uint32_t ni = (uint32_t)((x & 15) | ((z & 15) << 4) | ((y & 15) << 8));
                    lo = (s->data[ni >> 1] >> ((ni & 1) << 2)) & 0x0F;
                }
                {
                    int y2 = y + 1;
                    const mcp_chunk_section_t* s2 = c->sections[y2 >> 4];
                    if (s2 && s2->data) {
                        uint32_t ni = (uint32_t)((x & 15) | ((z & 15) << 4) | ((y2 & 15) << 8));
                        hi = (s2->data[ni >> 1] >> ((ni & 1) << 2)) & 0x0F;
                    }
                }
                uint32_t ci = (uint32_t)((x << 11) | (z << 7) | y);
                buf[kBlocks + (ci >> 1)] = (hi << 4) | lo;
            }
        }
    }
    off += kData;

    // --- Sky light: real per-column values from mcp_chunk_propagate_sky_light. ---
    // REAL BUG (black world/grass): this used to be a fixed memset(0xFF)
    // that ignored the computed light entirely. Fix: read the real
    // mcp_chunk_get_sky_light() values, same nibble packing as the
    // "Block data" section above.
    for (int x = 0; x < 16; ++x) {
        for (int z = 0; z < 16; ++z) {
            for (int y = 0; y < 128; y += 2) {
                uint8_t lo = mcp_chunk_get_sky_light(c, x, y, z) & 0x0F;
                uint8_t hi = mcp_chunk_get_sky_light(c, x, y + 1, z) & 0x0F;
                uint32_t ci = (uint32_t)((x << 11) | (z << 7) | y);
                buf[off + (ci >> 1)] = (hi << 4) | lo;
            }
        }
    }
    off += kSkyLight;

    // --- Block light: real values, same nibble packing as sky light ---
    // BUG REAL (antorchas invisibles): esto mandaba kBlockLight bytes de
    // cero sin mirar el chunk. Aunque el servidor calculase y guardase
    // la luz de una antorcha (TileFurnace/Torch + el pase de
    // generacion), el cliente recibia un array de block light todo en
    // cero en cada FullChunkDataPacket — es decir, la luz de antorchas
    // se quedaba enteramente en el servidor y nunca se veia en el juego.
    for (int x = 0; x < 16; ++x) {
        for (int z = 0; z < 16; ++z) {
            for (int y = 0; y < 128; y += 2) {
                uint8_t lo = mcp_chunk_get_block_light(c, x, y, z) & 0x0F;
                uint8_t hi = mcp_chunk_get_block_light(c, x, y + 1, z) & 0x0F;
                uint32_t ci = (uint32_t)((x << 11) | (z << 7) | y);
                buf[off + (ci >> 1)] = (hi << 4) | lo;
            }
        }
    }
    off += kBlockLight;

    // --- Heightmap: one byte per column (max y with non-air block, capped at 127) ---
    if (c->height_map) {
        for (int x = 0; x < 16; ++x) {
            for (int z = 0; z < 16; ++z) {
                int32_t h = c->height_map[(x & 15) + (z & 15) * 16];
                if (h > 127)
                    h = 127;
                buf[off + (x & 15) + (z & 15) * 16] = (uint8_t)h;
            }
        }
    }
    off += kHeightMap;

    // --- Biome colors: BE uint32 por columna = (biomeId<<24)|(R<<16)|(G<<8)|B.
    // REAL BUG (global black grass): an earlier fix assumed the MODERN
    // (1.0+) client behavior where the client computes grass tint from
    // the biome id. Protocol 84 clients (MCPE 0.15.x) render exactly
    // the RGB the server sends — R=G=B=0 paints grass black, the exact
    // reported bug.
    //
    // Fix: keep the biome id in the high byte (harmless, used for
    // IO/leveldb) and write the real per-biome grasscolor RGB into the
    // low 3 bytes, like the reference server did before API
    // 3.0.0-ALPHA1 (see World::setBiomeColor()).
    for (int x = 0; x < 16; ++x) {
        for (int z = 0; z < 16; ++z) {
            int idx = (x & 15) + (z & 15) * 16;
            uint8_t biomeId = c->biomes ? c->biomes[idx] : 1; // 1 = plains si no hay array
            uint8_t r, g, b;
            mcp_biome_grass_color(biomeId, &r, &g, &b);
            size_t o = off + (size_t)idx * 4;
            buf[o + 0] = biomeId;
            buf[o + 1] = r;
            buf[o + 2] = g;
            buf[o + 3] = b;
        }
    }
    off += kBiomeCol;

    // --- Extra data: LInt32 count = 0 ---
    buf[off + 0] = 0;
    buf[off + 1] = 0;
    buf[off + 2] = 0;
    buf[off + 3] = 0;
    off += 4;

    *out_len = off;
    return buf;
}
