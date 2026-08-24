// SPDX-License-Identifier: LGPL-3.0-or-later
// Chunk — raw in-memory chunk, pure C struct.
//
// A Chunk holds 16 ChunkSections stacked on Y (0..15), giving 16*16*256
// = 65536 blocks total. Each section is 16*16*16 = 4096 blocks. This
// matches the MCPE 0.15.x chunk model that atmkma / leveldb / mca
// formats all serialize.
//
// Pure C (not C++): the storage layer is C so it can be used from
// the RakLib thread and from generators without dragging C++ runtime
// overhead. The FullChunk C++ interface wraps this struct.
//
// Memory model: a Chunk owns all 16 section pointers (each
// malloc'd/free'd via mcp_chunk_new/mcp_chunk_destroy). A section
// may be NULL if the chunk hasn't been populated at that Y-range yet
// (lazy allocation, like Minecraft Java).
#pragma once

#include "ChunkSection.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// 256-byte per-column biome array + 256-int heightmap live on the
// Chunk (not on the Section). A "column" is (x, z); the per-section
// blocks live in the section_t above.
typedef struct mcp_chunk {
    int32_t  x;                  // chunk X coordinate
    int32_t  z;                  // chunk Z coordinate
    int64_t  last_update;        // unix ms of last disk write
    bool     terrain_populated;  // generator ran terrain pass
    bool     light_populated;    // generator ran light pass
    bool     dirty;              // has unsaved block writes

    mcp_chunk_section_t* sections[16]; // NULL = not yet allocated
    uint8_t* biomes;            // 256 bytes (one byte per column)
    int32_t* height_map;        // 256 ints (one int per column)

    // Tile-entity and entity payload lists. Each entry is an opaque
    // NBT blob (CompoundTag) — we store raw bytes for the C storage
    // layer; the C++ Level takes ownership of decoded entities.
    uint8_t** tile_entities;     // array of malloc'd NBT blobs
    size_t*  tile_entity_lens;  // matching lengths
    size_t   tile_entity_count;
    size_t   tile_entity_cap;

    uint8_t** entities;          // same shape as tile_entities above
    size_t*  entity_lens;
    size_t   entity_count;
    size_t   entity_cap;
} mcp_chunk_t;

// Lifecycle.
mcp_chunk_t* mcp_chunk_new(int32_t x, int32_t z);
void         mcp_chunk_destroy(mcp_chunk_t* c);

// Section management. `y` is a section index 0..15 (NOT a block Y!).
// mcp_chunk_get_or_alloc_section returns the existing section or
// allocates + zeroes a new one.
mcp_chunk_section_t* mcp_chunk_get_section(mcp_chunk_t* c, uint8_t y);
mcp_chunk_section_t* mcp_chunk_get_or_alloc_section(mcp_chunk_t* c, uint8_t y);

// Block access. Coordinates are chunk-local (x: 0..15, y: 0..255,
// z: 0..15). Out-of-range reads return 0; out-of-range writes are
// silently dropped.
uint8_t mcp_chunk_get_block_id   (const mcp_chunk_t* c, int x, int y, int z);
uint8_t mcp_chunk_get_block_data (const mcp_chunk_t* c, int x, int y, int z);
void    mcp_chunk_set_block_full (mcp_chunk_t* c, int x, int y, int z,
                                    uint8_t block_id, uint8_t data);
void    mcp_chunk_set_block_id   (mcp_chunk_t* c, int x, int y, int z, uint8_t id);
void    mcp_chunk_set_block_data (mcp_chunk_t* c, int x, int y, int z, uint8_t data);

// Lighting.
uint8_t mcp_chunk_get_sky_light  (const mcp_chunk_t* c, int x, int y, int z);
void    mcp_chunk_set_sky_light  (mcp_chunk_t* c, int x, int y, int z, uint8_t v);
uint8_t mcp_chunk_get_block_light(const mcp_chunk_t* c, int x, int y, int z);
void    mcp_chunk_set_block_light(mcp_chunk_t* c, int x, int y, int z, uint8_t v);

// Biome + heightmap.
uint8_t mcp_chunk_get_biome (const mcp_chunk_t* c, int x, int z);
void    mcp_chunk_set_biome (mcp_chunk_t* c, int x, int z, uint8_t b);
int32_t mcp_chunk_get_height_map(const mcp_chunk_t* c, int x, int z);
void    mcp_chunk_set_height_map(mcp_chunk_t* c, int x, int z, int32_t h);
void    mcp_chunk_recalculate_height_map(mcp_chunk_t* c);

// Tile-entity + entity add/remove. Copies the bytes in (caller still
// owns the original buffer). Returns 0 on success.
int mcp_chunk_add_tile_entity (mcp_chunk_t* c, const uint8_t* nbt, size_t len);
int mcp_chunk_add_entity       (mcp_chunk_t* c, const uint8_t* nbt, size_t len);
void mcp_chunk_clear_tile_entities(mcp_chunk_t* c);
void mcp_chunk_clear_entities      (mcp_chunk_t* c);

// Recalculate `non_air_top` flags after a bulk edit (used by light
// pass). Iterates every section's `blocks` array.
void mcp_chunk_recompute_section_flags(mcp_chunk_t* c);

// Serialize to a flat network-ready buffer suitable for
// FullChunkDataPacket (0x34). Format: 16 sections * (blocks+data+sky+
// blocklight) + 256 biomes + 256 ints heightmap + extra columns.
// Caller frees. *out_len receives size.
uint8_t* mcp_chunk_network_serialize(const mcp_chunk_t* c, size_t* out_len);

#ifdef __cplusplus
} // extern "C"
#endif
