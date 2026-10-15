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
#include "world/generator/object/ObjectDecorators.h"
#include "block/BlockIds.h"

namespace cursestone::world::generator::object {

using cursestone::block::BlockId;

namespace {

constexpr int kMaxY = 127;

inline bool inXZ(int v) {
    return v >= 0 && v < 16;
}
inline bool inY(int y) {
    return y >= 0 && y <= kMaxY;
}

inline uint8_t idAt(const mcp_chunk_t* c, int x, int y, int z) {
    if (!inXZ(x) || !inXZ(z) || !inY(y))
        return 0;
    return mcp_chunk_get_block_id(c, x, y, z);
}

inline bool isAir(const mcp_chunk_t* c, int x, int y, int z) {
    return idAt(c, x, y, z) == (uint8_t)BlockId::AIR;
}

inline bool isWater(uint8_t id) {
    return id == (uint8_t)BlockId::WATER || id == (uint8_t)BlockId::FLOWING_WATER;
}

inline bool isSoil(uint8_t id) {
    return id == (uint8_t)BlockId::GRASS || id == (uint8_t)BlockId::DIRT ||
           id == (uint8_t)BlockId::FARMLAND;
}

inline void put(mcp_chunk_t* c, int x, int y, int z, uint8_t id, uint8_t data) {
    if (!inXZ(x) || !inXZ(z) || !inY(y))
        return;
    mcp_chunk_set_block_full(c, x, y, z, id, data);
}

} // namespace

void ObjectDecorators::cactus(mcp_chunk_t* c, int x, int y, int z, MersenneRandom& rng) {
    if (!inXZ(x) || !inXZ(z))
        return;
    if (mcp_chunk_get_block_id(c, x, y, z) != (uint8_t)BlockId::SAND)
        return;
    int h = 1 + (int)rng.nextInt(3);
    for (int i = 1; i <= h; ++i) {
        if (!isAir(c, x, y + i, z))
            return;
        // Cactus must not touch solid blocks horizontally.
        if (idAt(c, x - 1, y + i, z) != (uint8_t)BlockId::AIR ||
            idAt(c, x + 1, y + i, z) != (uint8_t)BlockId::AIR ||
            idAt(c, x, y + i, z - 1) != (uint8_t)BlockId::AIR ||
            idAt(c, x, y + i, z + 1) != (uint8_t)BlockId::AIR) {
            return;
        }
    }
    for (int i = 1; i <= h; ++i)
        put(c, x, y + i, z, (uint8_t)BlockId::CACTUS_BIG, 0);
}

void ObjectDecorators::sugarCane(mcp_chunk_t* c, int x, int y, int z, MersenneRandom& rng) {
    if (!inXZ(x) || !inXZ(z))
        return;
    uint8_t ground = mcp_chunk_get_block_id(c, x, y, z);
    if (!isSoil(ground) && ground != (uint8_t)BlockId::SAND)
        return;

    bool waterAdjacent = isWater(idAt(c, x - 1, y, z)) || isWater(idAt(c, x + 1, y, z)) ||
                         isWater(idAt(c, x, y, z - 1)) || isWater(idAt(c, x, y, z + 1));
    bool caneAdjacent = idAt(c, x - 1, y + 1, z) == (uint8_t)BlockId::SUGARCANE ||
                        idAt(c, x + 1, y + 1, z) == (uint8_t)BlockId::SUGARCANE ||
                        idAt(c, x, y + 1, z - 1) == (uint8_t)BlockId::SUGARCANE ||
                        idAt(c, x, y + 1, z + 1) == (uint8_t)BlockId::SUGARCANE;
    if (!waterAdjacent && !caneAdjacent)
        return;

    int h = 1 + (int)rng.nextInt(3);
    for (int i = 1; i <= h; ++i)
        if (isAir(c, x, y + i, z))
            put(c, x, y + i, z, (uint8_t)BlockId::SUGARCANE, 0);
}

void ObjectDecorators::deadBush(mcp_chunk_t* c, int x, int y, int z) {
    if (mcp_chunk_get_block_id(c, x, y, z) == (uint8_t)BlockId::SAND && isAir(c, x, y + 1, z))
        put(c, x, y + 1, z, (uint8_t)BlockId::DEAD_BUSH, 0);
}

void ObjectDecorators::tallGrass(mcp_chunk_t* c, int x, int y, int z, uint8_t data) {
    if (isSoil(mcp_chunk_get_block_id(c, x, y, z)) && isAir(c, x, y + 1, z))
        put(c, x, y + 1, z, (uint8_t)BlockId::TALL_GRASS, data);
}

void ObjectDecorators::flower(mcp_chunk_t* c, int x, int y, int z, uint8_t id) {
    if (isSoil(mcp_chunk_get_block_id(c, x, y, z)) && isAir(c, x, y + 1, z))
        put(c, x, y + 1, z, id, 0);
}

void ObjectDecorators::doublePlant(mcp_chunk_t* c, int x, int y, int z, uint8_t data) {
    if (!isSoil(mcp_chunk_get_block_id(c, x, y, z)))
        return;
    if (!isAir(c, x, y + 1, z) || !isAir(c, x, y + 2, z))
        return;
    put(c, x, y + 1, z, (uint8_t)BlockId::DOUBLE_PLANT, (uint8_t)(data & 0x07));
    put(c, x, y + 2, z, (uint8_t)BlockId::DOUBLE_PLANT, (uint8_t)(0x08 | (data & 0x07)));
}

void ObjectDecorators::mushroom(mcp_chunk_t* c, int x, int y, int z, uint8_t id) {
    uint8_t g = mcp_chunk_get_block_id(c, x, y, z);
    if ((isSoil(g) || g == (uint8_t)BlockId::MYCELIUM) && isAir(c, x, y + 1, z))
        put(c, x, y + 1, z, id, 0);
}

void ObjectDecorators::pumpkin(mcp_chunk_t* c, int x, int y, int z, MersenneRandom& rng) {
    if (isSoil(mcp_chunk_get_block_id(c, x, y, z)) && isAir(c, x, y + 1, z))
        put(c, x, y + 1, z, (uint8_t)BlockId::PUMPKIN, (uint8_t)rng.nextInt(4));
}

void ObjectDecorators::melon(mcp_chunk_t* c, int x, int y, int z) {
    if (isSoil(mcp_chunk_get_block_id(c, x, y, z)) && isAir(c, x, y + 1, z))
        put(c, x, y + 1, z, (uint8_t)BlockId::MELON_BLOCK, 0);
}

void ObjectDecorators::waterLily(mcp_chunk_t* c, int x, int y, int z) {
    if (isWater(mcp_chunk_get_block_id(c, x, y, z)) && isAir(c, x, y + 1, z))
        put(c, x, y + 1, z, (uint8_t)BlockId::LILY_PAD, 0);
}

void ObjectDecorators::sapling(mcp_chunk_t* c, int x, int y, int z, uint8_t data) {
    if (isSoil(mcp_chunk_get_block_id(c, x, y, z)) && isAir(c, x, y + 1, z))
        put(c, x, y + 1, z, (uint8_t)BlockId::SAPLING, data);
}

} // namespace cursestone::world::generator::object
