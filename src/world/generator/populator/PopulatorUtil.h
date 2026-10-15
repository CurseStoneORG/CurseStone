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
// PopulatorUtil — shared helpers for the terrain feature populators.
#pragma once
#include "world/format/Chunk.h"
#include "world/generator/noise/MersenneRandom.h"
#include "block/BlockIds.h"

namespace cursestone::world::generator::populator {

// Deterministic per (chunk, salt) RNG so populators are order-independent.
inline MersenneRandom chunkRandom(int64_t seed, int32_t chunkX, int32_t chunkZ, uint64_t salt) {
    uint64_t mix = (uint64_t)seed ^ ((uint64_t)(uint32_t)chunkX * 341873128712ULL) ^
                   ((uint64_t)(uint32_t)chunkZ * 132897987541ULL) ^ (salt * 0x9E3779B97F4A7C15ULL);
    return MersenneRandom((int64_t)mix);
}

// Biome ids that never grow flowers/tall grass (sand/ice/water).
inline bool biomeHasVegetation(uint8_t b) {
    switch (b) {
    case 0:  // ocean
    case 10: // frozen ocean
    case 11: // frozen river
    case 2:  // desert
    case 17: // desert hills
    case 16: // beach
        return false;
    default:
        return true;
    }
}

// Cold biomes (snow layer / ice / spruce trees).
inline bool biomeIsCold(uint8_t b) {
    switch (b) {
    case 5:  // taiga
    case 12: // ice plains
    case 13: // ice mountains
    case 19: // taiga hills
    case 30: // cold taiga
    case 31: // cold taiga hills
    case 4:  // extreme hills (snowy peaks)
        return true;
    default:
        return false;
    }
}

// Y of the topmost "ground" block in a column (skips air, water, leaves,
// logs and thin vegetation). Returns -1 if the column is empty.
inline int surfaceY(const mcp_chunk_t* c, int x, int z) {
    using cursestone::block::BlockId;
    for (int y = 127; y >= 0; --y) {
        uint8_t id = mcp_chunk_get_block_id(c, x, y, z);
        switch (id) {
        case (uint8_t)BlockId::AIR:
        case (uint8_t)BlockId::WATER:
        case (uint8_t)BlockId::FLOWING_WATER:
        case (uint8_t)BlockId::LAVA:
        case (uint8_t)BlockId::FLOWING_LAVA:
        case (uint8_t)BlockId::LEAVES:
        case (uint8_t)BlockId::LEAVES_2:
        case (uint8_t)BlockId::LOG:
        case (uint8_t)BlockId::WOOD_2:
        case (uint8_t)BlockId::TALL_GRASS:
        case (uint8_t)BlockId::DEAD_BUSH:
        case (uint8_t)BlockId::FLOWER_YELLOW:
        case (uint8_t)BlockId::FLOWER_RED:
        case (uint8_t)BlockId::BROWN_MUSHROOM:
        case (uint8_t)BlockId::RED_MUSHROOM:
        case (uint8_t)BlockId::SNOW_LAYER:
        case (uint8_t)BlockId::SUGARCANE:
        case (uint8_t)BlockId::DOUBLE_PLANT:
            continue;
        default:
            return y;
        }
    }
    return -1;
}

} // namespace cursestone::world::generator::populator
