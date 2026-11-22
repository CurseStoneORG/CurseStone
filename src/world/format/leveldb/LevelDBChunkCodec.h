/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
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
// LevelDBChunkCodec — the MCPE 0.15.x records that ride inside the db/.
//
// The device writes one record family per chunk (little-endian everywhere):
//
//   key = int32 LE chunkX + int32 LE chunkZ + 1 record byte
//   0x30 "0" LegacyTerrain  blocks(32768) + data(16384) + sky(16384) +
//                           block light(16384) + heightmap(256) +
//                           biome colors(1024) = 83200 bytes
//                           (column layout, index = x<<11 | z<<7 | y)
//   0x66 "f" flags          1 byte: bit0 generated | bit1 populated |
//                           bit2 light populated (default 0x03)
//   0x76 "v" version        1 byte chunk format version (0x02)
//   0x31 "1" tiles          stream of little-endian NBT compounds
//   0x32 "2" entities       same stream layout
//
// This codec turns those records into/from the in-memory mcp_chunk_t, and
// handles level.dat: [int32 LE storage version = 3][int32 LE size]
// [uncompressed little-endian NBT] — the same header the real client's
// ExternalFileLevelStorage writes.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "../Chunk.h" // mcp_chunk_t + the wire snapshot

namespace cursestone::nbt {
class Tag;
}

namespace cursestone::world::format::leveldb {

class LevelDBChunkCodec {
  public:
    // --- chunk records ----------------------------------------------------

    // Record byte for each key family (the real MCPE LevelChunk tags).
    static constexpr uint8_t kRecordTerrain = 0x30;      // "0"
    static constexpr uint8_t kRecordTiles = 0x31;        // "1"
    static constexpr uint8_t kRecordEntities = 0x32;     // "2"
    static constexpr uint8_t kRecordFlags = 0x66;        // "f"
    static constexpr uint8_t kRecordVersion = 0x76;      // "v"
    static constexpr uint8_t kLegacyChunkVersion = 0x02; // device value
    static constexpr size_t kLegacyTerrainSize = 83200;

    // Builds the 9-byte db key: LE int32 x, LE int32 z, record byte.
    static std::vector<uint8_t> record_key(int32_t cx, int32_t cz, uint8_t record);

    // Inverse of mcp_chunk_snapshot_legacy_terrain_serialize: builds a live
    // mcp_chunk_t (sections in XZY, heightmap, biomes, light) from the
    // 83200-byte legacy record. Returns NULL on malformed input. Caller
    // owns the chunk (mcp_chunk_destroy).
    static mcp_chunk_t* decode_legacy_terrain(const uint8_t* data, size_t len,
                                              int32_t cx, int32_t cz);

    // Splits a "1"/"2" record into individual little-endian compound blobs
    // (one per tile entity / entity) without materializing a tag tree —
    // each blob is a byte range ready for mcp_chunk_add_tile_entity.
    static bool split_compound_stream(const uint8_t* data, size_t len,
                                      std::vector<std::vector<uint8_t>>* out);

    // --- level.dat ---------------------------------------------------------

    // Reads + parses level.dat (or level.dat_old as fallback). On success
    // *root owns the parsed little-endian NBT root compound and
    // *storage_version is the header version (3 on every modern file).
    static bool read_level_dat(const std::string& world_dir,
                               cursestone::nbt::Tag** root,
                               int32_t* storage_version);

    // Serializes `root` (named little-endian compound) with the
    // [version 3][size][NBT] header and writes it as level.dat; the
    // previous level.dat, when present, is renamed to level.dat_old first
    // (the device's rotate-on-save behavior and the crash-safety seam).
    static bool write_level_dat(const std::string& world_dir,
                                const cursestone::nbt::Tag* root);
};

} // namespace cursestone::world::format::leveldb
