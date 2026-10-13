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
// TreeGenerator — thin dispatcher: picks the species class for a TreeType
// and generates it. Each species lives in object/trees/<Type>Tree.{h,cpp}.
#pragma once
#include "world/format/Chunk.h"
#include "world/generator/noise/MersenneRandom.h"
#include "world/generator/object/TreeType.h"
#include <cstdint>

namespace cursestone::world::generator::object {

class TreeGenerator {
public:
    // `x,z` are coordinates relative to the target chunk and MAY be outside
    // 0..15: a tree rooted in a neighbouring chunk writes only the blocks
    // that overlap this chunk (they are clipped), which is what lets trees
    // span chunk borders. `y` is the first trunk block (ground + 1) and
    // `soilId` the block directly below it. Returns true if a tree was
    // placed.
    static bool place(mcp_chunk_t* c, int x, int y, int z, TreeType t,
                      uint8_t soilId, MersenneRandom& rng);
};

} // namespace cursestone::world::generator::object
