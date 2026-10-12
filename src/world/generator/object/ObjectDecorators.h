// SPDX-License-Identifier: LGPL-3.0-or-later
// ObjectDecorators — small vanilla terrain features (vegetation, cacti,
// sugar cane, lakeside props...). Ported/adapted from
// NetherGamesMC/ext-vanillagenerator lib/generator/object + decorators.
//
// Every `y` parameter is the Y of the top solid ground block; the feature
// is placed at y+1. All functions clip to the 16x16 chunk and to the
// MCPE 0.15.x 128-block height.
#pragma once
#include "world/format/Chunk.h"
#include "world/generator/noise/MersenneRandom.h"
#include <cstdint>

namespace cursestone::world::generator::object {

class ObjectDecorators {
public:
    static void cactus(mcp_chunk_t* c, int x, int y, int z, MersenneRandom& rng);
    static void sugarCane(mcp_chunk_t* c, int x, int y, int z, MersenneRandom& rng);
    static void deadBush(mcp_chunk_t* c, int x, int y, int z);
    static void tallGrass(mcp_chunk_t* c, int x, int y, int z, uint8_t data);
    static void flower(mcp_chunk_t* c, int x, int y, int z, uint8_t id);
    static void doublePlant(mcp_chunk_t* c, int x, int y, int z, uint8_t data);
    static void mushroom(mcp_chunk_t* c, int x, int y, int z, uint8_t id);
    static void pumpkin(mcp_chunk_t* c, int x, int y, int z, MersenneRandom& rng);
    static void melon(mcp_chunk_t* c, int x, int y, int z);
    static void waterLily(mcp_chunk_t* c, int x, int y, int z);
    static void sapling(mcp_chunk_t* c, int x, int y, int z, uint8_t data);
};

} // namespace cursestone::world::generator::object
