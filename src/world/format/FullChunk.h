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
// FullChunk — abstract "chunk ready to be sent" interface. C++.
//
// PocketMine's `FullChunk` interface sits between the storage layer
// (WorldProvider) and the runtime World. A FullChunk has enough state
// to be serialized into a `FullChunkDataPacket` (protocol 0x34) and
// broadcast to players.
//
// Concrete implementers: Chunk (level/format/Chunk.h, pure C struct
// wrapped by ChunkManager), and any future custom chunk type. The
// interface itself is pure-virtual C++ so the rest of the engine
// (World, Player chunk-sender) can hold a `FullChunk*` without
// caring which on-disk format produced it.
#pragma once

#include <cstdint>
#include <cstddef>
#include "../../CurseStone.h"

namespace cursestone::world::format {

class FullChunk {
  public:
    virtual ~FullChunk() = default;

    // Chunk coordinates in absolute world units (chunk X = floor(x / 16)).
    virtual int32_t getX() const = 0;
    virtual int32_t getZ() const = 0;

    // Geographic identity of the provider that loaded this chunk.
    // Used to route save requests back to the correct provider.
    virtual const char* getProviderName() const = 0;

    // Per-block access (Y up, 0..255 for MCPE 0.15.x which is 256-tall).
    // `fullState` packs (blockId << 4) | (data << 0) for 9-bit blocks;
    // use getBlock() / getBlockData() for the split form.
    virtual uint8_t getBlockId(int x, int y, int z) const = 0;
    virtual uint8_t getBlockData(int x, int y, int z) const = 0;
    virtual void setBlock(int x, int y, int z, uint8_t blockId, uint8_t data) = 0;

    // Sky/block light arrays are 2048 bytes each (4-bit nibbles).
    virtual uint8_t getSkyLight(int x, int y, int z) const = 0;
    virtual void setSkyLight(int x, int y, int z, uint8_t val) = 0;
    virtual uint8_t getBlockLight(int x, int y, int z) const = 0;
    virtual void setBlockLight(int x, int y, int z, uint8_t val) = 0;

    // 256-byte biome array (one byte per column).
    virtual uint8_t getBiome(int x, int z) const = 0;
    virtual void setBiome(int x, int z, uint8_t biomeId) = 0;

    // 256-int heightmap (one int per column). Used by the client for
    // shadowing and grass-tint.
    virtual int32_t getHeightMap(int x, int z) const = 0;
    virtual void setHeightMap(int x, int z, int32_t height) = 0;

    // Dirty flag — set whenever a block/light/biome write occurs, so
    // WorldProvider::saveChunk() knows whether to skip the chunk.
    virtual bool isPopulated() const = 0;
    virtual bool isGenerated() const = 0;
    virtual bool isDirty() const = 0;
    virtual void setDirty(bool dirty) = 0;

    // Serialize to a network-ready payload (the body of
    // FullChunkDataPacket, sans 0x34 header). Caller owns the buffer.
    // Implementation lives in the concrete chunk type; this is what
    // the chunk-send pipeline calls when a player asks for a chunk.
    virtual std::vector<uint8_t> networkSerialize() const = 0;
};

} // namespace cursestone::world::format
