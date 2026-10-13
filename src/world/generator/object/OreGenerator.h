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
// OreGenerator — places ore veins inside a chunk.
//
// Vein shape and the per-ore distribution are ported from
// NetherGamesMC/ext-vanillagenerator (object/OreVein + populators/
// OrePopulator): a vein is a chain of ellipsoids along a short random
// line, replacing only the target block (stone by default).
#pragma once
#include "world/format/Chunk.h"
#include <cstdint>

namespace cursestone::world::generator::object {

struct OreVein {
    uint8_t block_id;       // ore block id (e.g. 16 = coal ore)
    uint8_t target_id;      // block it replaces (1 = stone)
    int     min_y;          // lowest Y a vein center can spawn
    int     max_y;          // highest Y
    int     amount;         // ~blocks per vein (ellipsoid size)
    int     total;          // vein attempts per chunk
};

class OreGenerator {
public:
    // Places veins based on a deterministic per-chunk random derived from
    // `seed`, the vein block id and the chunk coordinates. Returns the
    // number of ore blocks placed.
    static int place(mcp_chunk_t* c, int64_t seed, const OreVein& v);
};

} // namespace cursestone::world::generator::object
