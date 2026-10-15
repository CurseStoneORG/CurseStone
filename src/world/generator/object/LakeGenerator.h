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
// LakeGenerator — small above-ground water/lava pools.
// Adapted from NetherGamesMC/ext-vanillagenerator lib/generator/object/Lake.
#pragma once
#include "world/format/Chunk.h"
#include "world/generator/noise/MersenneRandom.h"
#include <cstdint>

namespace cursestone::world::generator::object {

class LakeGenerator {
  public:
    // Carve an ellipsoidal pool centered on (x,y,z) (chunk-local).
    // liquidId is WATER (9) or LAVA (11). Returns true if one was carved.
    static bool place(mcp_chunk_t* c, int x, int y, int z, uint8_t liquidId, MersenneRandom& rng);
};

} // namespace cursestone::world::generator::object
