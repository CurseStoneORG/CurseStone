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
// ChunkLoader — abstract consumer of chunk updates.
// A Player is the canonical ChunkLoader: it asks the World to load
// chunks within view distance and the World pushes FullChunkDataPacket
// to it. Mirrors level/ChunkLoader interface.
#pragma once
#include "world/format/Chunk.h"
namespace cursestone::world {
class ChunkLoader {
  public:
    virtual ~ChunkLoader() = default;
    virtual void onChunkLoaded(int32_t x, int32_t z, const mcp_chunk_t* c) = 0;
    virtual int32_t viewDistance() const {
        return 8;
    }
    virtual bool isAlive() const {
        return true;
    }
};
} // namespace
