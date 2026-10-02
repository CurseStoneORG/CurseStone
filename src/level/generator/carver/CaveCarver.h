// SPDX-License-Identifier: LGPL-3.0-or-later
// CaveCarver — vanilla cave generator.
//
// Ported from NetherGamesMC/ext-vanillagenerator
// (lib/generator/overworld/carver/CaveCarver). It carves the classic
// "spaghetti" tunnels plus occasional larger rooms using a random walk of
// ellipsoids, exactly like the reference. Adapted to:
//   * our mcp_chunk_t block API (legacy numeric block ids),
//   * the MCPE 0.15.x 128-block world height (topY = 128),
//   * a per-chunk deterministic seed so results do not depend on the
//     order async workers happen to generate chunks in.
#pragma once
#include "level/format/Chunk.h"
#include "level/generator/noise/GenRandom.h"
#include <cstdint>

namespace cursestone::level::generator::carver {

class CaveCarver {
public:
    // Carve caves into `chunk` (which must already have raw stone terrain).
    void generate(int64_t seed, int32_t chunkX, int32_t chunkZ, mcp_chunk_t* chunk);

private:
    void recursiveGenerate(int32_t chunkX, int32_t chunkZ,
                           int32_t originChunkX, int32_t originChunkZ,
                           mcp_chunk_t* chunk, bool addRooms);
    void addRoom(int64_t seed, int32_t originChunkX, int32_t originChunkZ,
                 mcp_chunk_t* chunk, double startX, double startY, double startZ);
    void addTunnel(int64_t seed, int32_t originChunkX, int32_t originChunkZ,
                   mcp_chunk_t* chunk, double startX, double startY, double startZ,
                   float width, float yaw, float pitch,
                   int32_t startCounter, int32_t endCounter, double heightModifier);
    void digBlock(mcp_chunk_t* chunk, int8_t x, int16_t y, int8_t z) const;
    bool canReplaceBlock(const mcp_chunk_t* chunk, int8_t x, int16_t y, int8_t z) const;

    static int  highestBlockAt(const mcp_chunk_t* c, int x, int z);
    static bool isLiquid(uint8_t id);

    static const int32_t kChunkRadius = 8;
    static const int32_t kDensity = 16;
    static const int32_t kTopY = 128;
    static const int32_t kBottomY = 8;
    static const int32_t kLiquidAltitude = 10;

    GenRandom rand_{0};
};

} // namespace cursestone::level::generator::carver
