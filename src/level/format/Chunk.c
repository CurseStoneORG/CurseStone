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
#include "../../block/BlockLightC.h"

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
// BUG REAL (chunks/luz corrupta): esto desplazaba `y` solo 7 bits en
// vez de 8. z ocupa los bits 4-7 (4 bits, z=0..15) y con el shift de
// y en 7, el bit más alto de z (que marca z>=8) y el bit más bajo de
// y&15 (que marca "y impar dentro de la sección") caían en el MISMO
// bit del índice. Resultado: (x, z=0, y) y (x, z=8, y) con el mismo
// y&15 impar colisionaban en el mismo byte físico — una escritura
// legítima en una celda pisaba silenciosamente el valor de otra
// celda completamente distinta, dependiendo del orden de escritura.
// Esto se veía como luz de cielo "corrupta" (15 recién escrito que
// aparecía en 0 poco después) y como franjas negras alineadas con
// z=8 (la mitad del chunk) en el render. block_index() de arriba ya
// hace esto bien (shift de 8, sin solapamiento) — nibble_index solo
// necesita el mismo esquema, reservando 4 bits limpios por eje.
static inline uint32_t nibble_index(int x, int y, int z) {
    return (uint32_t)((x & 15) | ((z & 15) << 4) | ((y & 15) << 8));
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
    if (s) return get_nibble(s->sky_light, x, y, z);
    // Sección no alocada: en la práctica esto solo pasa para aire
    // puro por encima de la superficie más alta del chunk. Cualquier
    // sección que tenga un bloque real ya fue alocada al colocarlo
    // (mcp_chunk_set_block_id/_full), y mcp_chunk_propagate_sky_light
    // aloca explícitamente toda sección dentro de [0, maxTop] del
    // chunk — así que "sección NULL" siempre implica cielo abierto,
    // nunca terreno sin iluminar. Cielo abierto = luz plena.
    return 15;
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
    // BUG REAL (pasto/mundo negro): esta función marcaba como "techo"
    // cualquier bloque NO-AIRE (`!= 0`), incluyendo hojas, flores,
    // hierba alta, carteles, etc. — bloques que en Minecraft NO
    // bloquean luz de cielo. El heightmap resultante quedaba más alto
    // de lo real bajo cualquier árbol/planta, y el pase de luz de
    // cielo (mcp_chunk_propagate_sky_light) usa este heightmap como
    // punto de partida para saber desde qué Y empieza a decrementar
    // la luz. Con el heightmap inflado, columnas con cielo
    // prácticamente abierto (solo una hoja o una flor de por medio)
    // se calculaban como si estuvieran bajo un techo sólido —
    // resultado: pasto renderizado negro cerca de cualquier árbol.
    //
    // Fix: solo cuenta como "techo" un bloque cuya opacidad de luz
    // (mcp_block_light_opacity) sea > 0, igual que Minecraft/PMMP
    // (Block::getLightFilter() > 0). Hojas, agua, vidrio, flores,
    // etc. son transparentes a la luz (opacidad 0) y ya no cuentan.
    for (int col = 0; col < 256; ++col) {
        int x = col & 15;
        int z = (col >> 4) & 15;
        int32_t h = 0;
        for (int y = 255; y >= 0; --y) {
            uint8_t id = mcp_chunk_get_block_id(c, x, y, z);
            if (id != 0 && mcp_block_light_opacity(id) > 0) {
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

// ---------------------------------------------------------------- sky light propagation
//
// BUG REAL (mundo/pasto negro): hasta ahora ningún generador calculaba
// luz de verdad — todos hacían `memset(sky_light, 0xFF, ...)` en cada
// sección alocada y marcaban light_populated=true. Eso pone luz PLENA
// (15) en cada bloque de cada sección, incluso bajo piedra sólida o
// bajo el dosel de un árbol. El cliente MCPE 0.15.x no re-calcula la
// oclusión por sí solo a partir de sky_light — usa exactamente lo que
// el servidor le manda. Con luz uniforme en vez de un degradado real,
// cualquier columna donde el heightmap (ya corregido arriba) todavía
// marcara "techo" cerca de superficie, y varias combinaciones de
// vecinos sin luz calculada, terminaban rindiendo negro sólido — el
// patrón exacto de la screenshot (pasto oscuro pegado a los árboles).
//
// Fix: pase real de dos fases, mismo enfoque que
// SimpleChunkManager::populateSkyLight de PMMP:
//   Fase 1 (columna): para cada (x,z), luz = 15 desde arriba del
//     heightmap; al bajar, cada bloque resta su opacidad
//     (mcp_block_light_opacity) al valor de luz que sigue bajando.
//   Fase 2 (flood-fill horizontal): la luz de cada bloque también se
//     copia a sus 4 vecinos horizontales menos (opacidad del vecino +
//     1), en cola, hasta estabilizar — así la luz se filtra de lado
//     bajo aleros/ramas en vez de quedar un pilar recto por columna.
// No toca block_light (luz de antorchas) — eso es un pase aparte que
// no hace falta para el bug de pasto negro.
void mcp_chunk_propagate_sky_light(mcp_chunk_t* c) {
    if (!c) return;

    // BUG REAL (chunks que no renderizan): la versión anterior barría
    // y de 255 a 0 para las 256 columnas, en las tres fases (columna,
    // semilla del BFS y el propio BFS). Eso fuerza
    // mcp_chunk_get_or_alloc_section() a alocar las 16 secciones de
    // CADA chunk generado, incluidas las que están completamente
    // vacías (puro cielo muy por encima de cualquier bloque) — solo
    // para volver a escribirles el mismo 15 que ya traen por defecto
    // (mcp_section_new ya inicializa sky_light a 0xFF). Multiplicado
    // por todos los chunks visibles generándose a la vez, esto
    // encarece tanto la generación async que algunos chunks llegan
    // tarde o ni llegan al cliente — que los pinta como huecos negros.
    //
    // Fix: acotar las tres fases a [0, maxTop], donde maxTop es la
    // columna más alta de TODO el chunk (no la de cada columna
    // individual — necesitamos ese margen para que la sombra lateral
    // bajo un árbol/estructura vecina se calcule bien). Por encima de
    // maxTop no hay un solo bloque en ningún (x,z) de este chunk, así
    // que ninguna sección de ahí necesita existir: mcp_chunk_get_sky_light
    // ya devuelve 15 para una sección no alocada.
    int32_t maxTop = 0;
    for (int col = 0; col < 256; ++col) {
        int32_t h = c->height_map ? c->height_map[col] : 0;
        if (h > maxTop) maxTop = h;
    }
    if (maxTop > 255) maxTop = 255;

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
                uint8_t op = mcp_block_light_opacity(id);
                // Minecraft resta (opacidad, mínimo 1 para que la luz
                // siga bajando algo incluso a través de "transparentes")
                // salvo que sea aire puro (opacidad 0 real → no resta).
                if (id != 0) {
                    int dec = op > 0 ? op : 1;
                    light -= dec;
                    if (light < 0) light = 0;
                }
                mcp_chunk_set_sky_light(c, x, y, z, (uint8_t)light);
                if (light <= 0) {
                    // ya no hay luz de cielo posible más abajo en esta
                    // columna por esta fase vertical — la fase 2
                    // (horizontal) se encarga de rellenar por los
                    // costados si corresponde.
                }
            }
        }
    }

    // --- Fase 2: flood-fill horizontal (BFS) para que la luz se
    // filtre bajo aleros/ramas en vez de quedar en columnas rectas ---
    // Cola simple de (x,y,z) a revisar, acotada a [0, maxTop] — por
    // encima de maxTop no hay nada que propagar (todo ya es 15).
    typedef struct { uint8_t x, z; uint16_t y; } LightNode;
    size_t cap = (size_t)16 * 16 * (size_t)(maxTop + 1);
    if (cap == 0) cap = 1;
    LightNode* queue = (LightNode*)malloc(cap * sizeof(LightNode));
    if (!queue) return; // sin memoria: nos quedamos con el resultado de la fase 1
    size_t qhead = 0, qtail = 0;

    // Semilla: toda celda con luz > 1 puede propagar a sus vecinas.
    for (int x = 0; x < 16; ++x) {
        for (int z = 0; z < 16; ++z) {
            for (int y = 0; y <= maxTop; ++y) {
                if (mcp_chunk_get_sky_light(c, x, y, z) > 1) {
                    queue[qtail++] = (LightNode){(uint8_t)x, (uint8_t)z, (uint16_t)y};
                }
            }
        }
    }

    static const int dx[4] = { 1, -1, 0, 0 };
    static const int dz[4] = { 0, 0, 1, -1 };

    while (qhead < qtail) {
        LightNode n = queue[qhead++];
        int lx = n.x, lz = n.z, ly = n.y;
        uint8_t curLight = mcp_chunk_get_sky_light(c, lx, ly, lz);
        if (curLight <= 1) continue;

        for (int dir = 0; dir < 4; ++dir) {
            int nx = lx + dx[dir];
            int nz = lz + dz[dir];
            // single-chunk pass: no cruzamos a chunks vecinos (ver
            // nota en Chunk.h) — solo nos quedamos dentro de 0..15.
            if (nx < 0 || nx > 15 || nz < 0 || nz > 15) continue;

            uint8_t nid = mcp_chunk_get_block_id(c, nx, ly, nz);
            uint8_t nop = mcp_block_light_opacity(nid);
            int dec = nop > 0 ? nop : 1;
            int candidate = (int)curLight - dec;
            if (candidate <= 0) continue;

            uint8_t existing = mcp_chunk_get_sky_light(c, nx, ly, nz);
            if (candidate > existing) {
                mcp_chunk_set_sky_light(c, nx, ly, nz, (uint8_t)candidate);
                if (candidate > 1 && qtail < cap) {
                    queue[qtail++] = (LightNode){(uint8_t)nx, (uint8_t)nz, (uint16_t)ly};
                }
            }
        }
    }

    free(queue);
    c->light_populated = true;
    c->dirty = true;
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
    // BUG REAL (mundo/pasto negro): antes esto era un memset(0xFF) fijo
    // que ignoraba por completo la luz calculada — pisaba cualquier
    // sombra real con "luz plena en todo". Si el pase de propagación
    // nunca corrió (light_populated era true sin haber calculado nada
    // real), el resultado visual dependía de cómo el cliente
    // interpretaba ese 0xFF uniforme combinado con el heightmap
    // (antes también roto — ver mcp_chunk_recalculate_height_map),
    // produciendo negro sólido bajo cualquier dosel de hojas.
    // Fix: leer mcp_chunk_get_sky_light() real, mismo empaquetado de
    // nibbles que usa el bloque de "Block data" de arriba.
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

    // --- Biome colors: BE uint32 per column = (biomeId<<24)|(R<<16)|(G<<8)|B.
    // BUG REAL (confirmado contra PMMP: level/format/generic/BaseFullChunk.php
    // setBiomeId() hace `biomeColors[i] = (biomeColors[i] & 0xFFFFFF) |
    // (biomeId << 24)` — el ID de bioma va en el BYTE ALTO de este entero
    // de 4 bytes, y R,G,B van en los 3 bytes bajos, no al revés).
    //
    // El código anterior escribía [R=0x00, G=0x80, B=0x40, sin_usar=0x00]
    // en esos mismos 4 bytes para TODAS las columnas, ignorando el bioma
    // real de cada una. Leído con el layout correcto, esos bytes son en
    // realidad [biomeId=0x00, R=0x80, G=0x40, B=0x00] — un marrón/naranja
    // oscuro (128,64,0), exactamente el color que se veía en el pasto. No
    // era un problema de terreno ni de generación de altura.
    //
    // Fix: byte alto = bioma real de la columna (ya calculado por el
    // generador y guardado en c->biomes[]); R=G=B=0 en los 3 bytes bajos,
    // que es lo que PMMP usa como "sin override de color" (su valor por
    // defecto es 0xFF000000: bioma inválido + RGB en cero) — así el
    // cliente calcula el tinte verde de pasto/hojas él mismo a partir del
    // ID de bioma real, con su propia tabla interna de grasscolor/
    // foliagecolor, en vez de que el servidor le imponga un color fijo.
    //
    // Se escribe con índice explícito (x&15)+(z&15)*16 — la MISMA fórmula
    // que usa mcp_chunk_set_biome()/mcp_chunk_get_biome() — en vez de
    // depender del orden del loop, para no introducir un desalineado
    // fila/columna entre lo que el generador guardó y lo que se manda.
    for (int x = 0; x < 16; ++x) {
        for (int z = 0; z < 16; ++z) {
            int idx = (x & 15) + (z & 15) * 16;
            uint8_t biomeId = c->biomes ? c->biomes[idx] : 1; // 1 = plains si no hay array
            size_t o = off + (size_t)idx * 4;
            buf[o + 0] = biomeId;
            buf[o + 1] = 0;
            buf[o + 2] = 0;
            buf[o + 3] = 0;
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
