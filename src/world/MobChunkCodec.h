/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
| \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

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
// MobChunkCodec — the serialization seam between the world's mob records
// and the chunk entity lists inside the region files (.mca).
//
// Mobs are chunk state, exactly like blocks and tile entities: every
// chunk's Anvil NBT carries an "Entities" list and each mob rides it as
// one little-endian NBT compound blob (the same blob channel
// mcp_chunk_t already round-trips through anvil_chunk_encode/decode).
// The World rebuilds a chunk's list from its live records whenever the
// mobs of that chunk changed, and re-creates records from the list when
// the chunk (re)enters the level — so a mob lives and dies with its
// chunk and there is no side file left to drift out of sync.
//
// Blob layout (one per mob, little-endian like every chunk-embedded
// blob; the region writer re-serializes big-endian):
//
//   CompoundTag {
//     "id"       : String  — registry name ("sheep", "zombie_pigman")
//     "Pos"      : List<FLOAT>[3] — feet position
//     "Rotation" : List<FLOAT>[2] — yaw, pitch
//     "Health"   : FLOAT
//     "MaxHealth": FLOAT
//   }
//
// The runtime eid is deliberately NOT stored (ids are allocated fresh
// whenever the chunk loads), and knockback / death-window state is
// transient by design. Structural sanity (finite numbers, world bounds,
// sane health ceilings) is checked here; registry membership is
// re-validated by the World on restore, so the format layer never
// drifts from the entity registry.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "world/format/Chunk.h" // mcp_chunk_t (chunk entity blob channel)

namespace cursestone::world {

// One persisted mob: the subset of world::MobEntity that survives a
// chunk unload or a restart. Plain struct on purpose — it is copied out
// of the world's mob map under the mob lock and dropped right after the
// write.
struct PersistedMob {
    int32_t type = 0; // Protocol-84 network id (entity::EntityTypes).
    double x = 0.0, y = 0.0, z = 0.0; // Feet position.
    float yaw = 0.0f, pitch = 0.0f;
    float health = 0.0f;
    float max_health = 0.0f;
};

class MobChunkCodec {
  public:
    // Serializes one mob as a little-endian NBT compound blob.
    static std::vector<uint8_t> encodeBlob(const PersistedMob& mob);

    // Parses one blob. Tolerant by contract: a corrupted row must never
    // cost the chunk its other mobs. Returns false for malformed or
    // out-of-range rows (missing id/Pos, non-finite numbers, world
    // bounds, dead health values); rotations are wrapped into the wire
    // ranges so hand-edited data cannot push a weird yaw through the
    // spawn packet.
    static bool decodeBlob(const uint8_t* data, size_t len, PersistedMob* out);

    // Replaces the chunk's whole entity list with fresh blobs for
    // `mobs`. Returns true when the on-chunk bytes actually changed —
    // the caller marks the chunk dirty only then, so an unchanged mob
    // roster never triggers a pointless chunk rewrite.
    static bool writeChunkEntities(mcp_chunk_t* chunk,
                                   const std::vector<PersistedMob>& mobs);

    // Reads every parsable mob row back out of a chunk's entity list.
    static std::vector<PersistedMob> readChunkEntities(const mcp_chunk_t* chunk);

    // One-time importer for the legacy side file ("<world
    // dir>/mobs.dat" of builds that persisted mobs outside the region
    // files): same tolerant row-by-row contract the old reader had, so
    // a hand-corrupted line costs one mob instead of the migration.
    static std::vector<PersistedMob> loadLegacyMobsDat(const std::string& path);
};

} // namespace cursestone::world
