/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
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
// Chunk — raw in-memory chunk, POD struct.
//
// A Chunk holds 16 ChunkSections stacked on Y (0..15), giving 16*16*256
// = 65536 blocks total. Each section is 16*16*16 = 4096 blocks. This
// matches the MCPE 0.15.x chunk model that the anvil / leveldb formats
// serialize (only the lower 8 sections are persisted, since MCPE 0.15.x
// is 128-tall).
//
// POD (not RAII): the storage layer stays memcpy/memset-able so it can be used from
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


// 256-byte per-column biome array + 256-int heightmap live on the
// Chunk (not on the Section). A "column" is (x, z); the per-section
// blocks live in the section_t above.
typedef struct mcp_chunk {
    int32_t x;              // chunk X coordinate
    int32_t z;              // chunk Z coordinate
    int64_t last_update;    // unix ms of last disk write
    bool terrain_populated; // generator ran terrain pass
    bool light_populated;   // generator ran light pass
    bool dirty;             // has unsaved block writes
    // LightEngine work flag: a neighbour's border push or a full sweep
    // deposited light INSIDE this chunk after its own pass ran. The
    // chunk's next border entry must run an incremental interior flood
    // to spread the deposit — without it, light written directly into
    // mid-chunk cells would freeze where it landed (the border pull
    // only floods from the wall cells).
    uint8_t light_touched;

    // Monotonic counter bumped by every live-chunk block write
    // (mcp_chunk_set_block_full / mcp_chunk_set_block_id). World uses it
    // to detect "did any block change since the last tile-hydration
    // scan" without rescanning the chunk's 32768 cells on every stream:
    // cost of a block edit stays O(1), cost of an unchanged chunk's
    // hydration check drops to a 32-bit compare. Generator-time direct
    // section writes intentionally do NOT bump it — those chunks are not
    // live yet, so their first hydration happens after install regardless.
    uint32_t block_edit_serial;

    mcp_chunk_section_t* sections[16]; // NULL = not yet allocated
    uint8_t* biomes;                   // 256 bytes (one byte per column)
    int32_t* height_map;               // 256 ints (one int per column)

    // Tile-entity and entity payload lists. Each entry is an opaque
    // NBT blob (CompoundTag) — we store raw bytes for the C storage
    // layer; the C++ World takes ownership of decoded entities.
    uint8_t** tile_entities;  // array of malloc'd NBT blobs
    size_t* tile_entity_lens; // matching lengths
    size_t tile_entity_count;
    size_t tile_entity_cap;

    uint8_t** entities; // same shape as tile_entities above
    size_t* entity_lens;
    size_t entity_count;
    size_t entity_cap;
} mcp_chunk_t;

// Lifecycle.
mcp_chunk_t* mcp_chunk_new(int32_t x, int32_t z);
void mcp_chunk_destroy(mcp_chunk_t* c);

// Section management. `y` is a section index 0..15 (NOT a block Y!).
// mcp_chunk_get_or_alloc_section returns the existing section or
// allocates + zeroes a new one.
mcp_chunk_section_t* mcp_chunk_get_section(mcp_chunk_t* c, uint8_t y);
mcp_chunk_section_t* mcp_chunk_get_or_alloc_section(mcp_chunk_t* c, uint8_t y);

// Block access. Coordinates are chunk-local (x: 0..15, y: 0..255,
// z: 0..15). Out-of-range reads return 0; out-of-range writes are
// silently dropped.
uint8_t mcp_chunk_get_block_id(const mcp_chunk_t* c, int x, int y, int z);
uint8_t mcp_chunk_get_block_data(const mcp_chunk_t* c, int x, int y, int z);
void mcp_chunk_set_block_full(mcp_chunk_t* c, int x, int y, int z, uint8_t block_id, uint8_t data);
void mcp_chunk_set_block_id(mcp_chunk_t* c, int x, int y, int z, uint8_t id);

// Lighting.
uint8_t mcp_chunk_get_sky_light(const mcp_chunk_t* c, int x, int y, int z);
void mcp_chunk_set_sky_light(mcp_chunk_t* c, int x, int y, int z, uint8_t v);
uint8_t mcp_chunk_get_block_light(const mcp_chunk_t* c, int x, int y, int z);
void mcp_chunk_set_block_light(mcp_chunk_t* c, int x, int y, int z, uint8_t v);

// Biome + heightmap.
uint8_t mcp_chunk_get_biome(const mcp_chunk_t* c, int x, int z);
void mcp_chunk_set_biome(mcp_chunk_t* c, int x, int z, uint8_t b);
int32_t mcp_chunk_get_height_map(const mcp_chunk_t* c, int x, int z);
void mcp_chunk_set_height_map(mcp_chunk_t* c, int x, int z, int32_t h);
void mcp_chunk_recalculate_height_map(mcp_chunk_t* c);
// Same result for ONE column only. mcp_chunk_set_block_full() only ever
// RAISES the height map, so removing a block (or digging a room) left
// the column marked as roofed at the old height — which then made the
// sky-light seed treat open air as solid ground. Cheap enough to call
// on every block placement.
void mcp_chunk_recalculate_height_map_column(mcp_chunk_t* c, int x, int z);

// Global sky light: every allocated section's sky nibbles are filled
// with 15 (the populateSkyLight / setAllBlockSkyLight(15) reference
// semantics — no shadows anywhere, caves included). Unallocated
// sections already read 15 through mcp_chunk_get_sky_light. Marks the
// chunk light_populated but never dirty.
void mcp_chunk_fill_sky_light(mcp_chunk_t* c);

// Light pass for a freshly generated chunk: global sky fill (above)
// plus the real block-light pass (seed from every block's emission,
// flood in the six vanilla directions). Must run AFTER
// mcp_chunk_recalculate_height_map — the block-light sweep is bounded
// by the chunk's highest terrain so empty sky sections are never
// allocated. Torches keep real local light on top of the global sky
// layer. Single-chunk only: block light crossing a border is healed by
// World's LightEngine once neighbours install. Sets light_populated.
void mcp_chunk_propagate_sky_light(mcp_chunk_t* c);

// Tile-entity + entity add/remove. Copies the bytes in (caller still
// owns the original buffer). Returns 0 on success.
int mcp_chunk_add_tile_entity(mcp_chunk_t* c, const uint8_t* nbt, size_t len);
// Drops one tile-entity blob by index (shifts the rest left) and marks
// the chunk dirty. No-op when the index is out of range.
void mcp_chunk_remove_tile_entity_at(mcp_chunk_t* c, size_t index);
int mcp_chunk_add_entity(mcp_chunk_t* c, const uint8_t* nbt, size_t len);
void mcp_chunk_clear_tile_entities(mcp_chunk_t* c);
void mcp_chunk_clear_entities(mcp_chunk_t* c);

// Recalculate `non_air_top` flags after a bulk edit (used by light
// pass). Iterates every section's `blocks` array.
void mcp_chunk_recompute_section_flags(mcp_chunk_t* c);

// Serialize to a flat network-ready buffer suitable for
// FullChunkDataPacket (0x34). Format: 16 sections * (blocks+data+sky+
// blocklight) + 256 biomes + 256 ints heightmap + extra columns.
// Caller frees. *out_len receives size.
uint8_t* mcp_chunk_network_serialize(const mcp_chunk_t* c, size_t* out_len);

// Wire snapshots — async chunk streaming support.
//
// mcp_chunk_network_serialize walks the LIVE chunk, so it is only safe on
// the thread that owns chunk writes (the main tick thread). To move the
// transposition off the tick thread, the main thread first takes a
// snapshot — a deep copy of exactly the data the serializer reads — and a
// worker then runs mcp_chunk_snapshot_network_serialize() on it. The two
// entry points share one transposition core, so their output bytes are
// identical by construction.
//
// Threading contract (mirrors the Anvil save pipeline):
//   * MAIN thread: mcp_chunk_wire_snapshot_create() reads the live chunk.
//     The main tick is the only writer of resident chunk data; generation
//     workers build their chunk BEFORE publishing it into ChunkManager and
//     never touch a published chunk again, so a snapshot taken during the
//     tick is a consistent immutable view.
//   * WORKER thread: mcp_chunk_snapshot_network_serialize() touches ONLY
//     the snapshot it is handed — never the world, never ChunkManager.
typedef struct mcp_chunk_wire_snapshot {
    int32_t x; // Chunk X (informational; the packet carries its own coords).
    int32_t z; // Chunk Z (informational).
    // Per sub-section 0..7: pointers are NULL when that array does not
    // exist on the live chunk. section_present[] distinguishes "section
    // object absent" (wire sky slice fills 0xFF) from "section present
    // with a NULL array" (wire slice reads zeros) — the serializer treats
    // the two differently and the snapshot must preserve the difference.
    uint8_t section_present[8]; // 0/1 per section.
    uint8_t* section_blocks[8]; // 4096 bytes each when present.
    uint8_t* section_data[8];   // 2048 bytes each when present.
    uint8_t* section_sky[8];    // 2048 bytes each when present.
    uint8_t* section_light[8];  // 2048 bytes each when present.
    uint8_t biomes[256];        // Copy of the live biomes array.
    int32_t height_map[256];    // Copy of the live heightmap.
} mcp_chunk_wire_snapshot_t;

/**
 * @brief Deep-copies the chunk data the network serializer reads.
 *
 * Runs on the main tick thread (the only writer of resident chunk data).
 * Cost is memcpy-bound: at most 8 sections of ~10 KB each.
 *
 * @param c  resident chunk to copy; must not be null.
 * @return an owned snapshot, or NULL on allocation failure. Free it with
 *         mcp_chunk_wire_snapshot_free().
 */
mcp_chunk_wire_snapshot_t* mcp_chunk_wire_snapshot_create(const mcp_chunk_t* c);

/**
 * @brief Frees a snapshot returned by mcp_chunk_wire_snapshot_create().
 * @param s  snapshot to free; NULL is allowed and ignored.
 */
void mcp_chunk_wire_snapshot_free(mcp_chunk_wire_snapshot_t* s);

/**
 * @brief Serializes a snapshot to the FullChunkDataPacket payload.
 *
 * Byte-identical to mcp_chunk_network_serialize() of the chunk the
 * snapshot was taken from. Safe on any thread: it touches only @p s.
 *
 * @param s        snapshot to serialize; must not be null.
 * @param out_len  receives the payload size; must not be null.
 * @return an owned 83,204-byte buffer, or NULL on allocation failure.
 *         Caller frees (free()).
 */
uint8_t* mcp_chunk_snapshot_network_serialize(const mcp_chunk_wire_snapshot_t* s,
                                              size_t* out_len);

/**
 * @brief Serializes a snapshot to the MCPE LevelDB legacy terrain record.
 *
 * The record the MCPE 0.15.x device writes under the "0" (LegacyTerrain)
 * db key: the wire payload minus the trailing extra-data count —
 * blocks + data + sky + block light + heightmap + biome colors
 * (83,200 bytes). Same transposition core as the wire serializer, so the
 * on-disk record and the streamed payload agree byte-for-byte on the
 * parts they share. Safe on any thread: it touches only @p s.
 *
 * @param s        snapshot to serialize; must not be null.
 * @param out_len  receives the record size (83,200); must not be null.
 * @return an owned buffer, or NULL on allocation failure. Caller frees.
 */
uint8_t* mcp_chunk_snapshot_legacy_terrain_serialize(
    const mcp_chunk_wire_snapshot_t* s, size_t* out_len);

