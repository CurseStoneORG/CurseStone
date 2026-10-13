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
// GenericTree — vanilla tree base class.
//
// Ported from NetherGamesMC/ext-vanillagenerator
// (lib/generator/object/trees/GenericTree). It implements the classic oak
// leaf-blob + trunk, and every other species lives in its own file next to
// this one overriding init()/generate()/placeCanopy().
//
// Coordinates: `sourceX/sourceZ` are chunk-local (0..15) and `sourceY` is
// the FIRST trunk block (i.e. ground + 1), matching the reference. Blocks
// are written directly to the chunk (no BlockTransaction) and anything
// outside the 16x16 column is clipped.
#pragma once
#include "world/format/Chunk.h"
#include "world/generator/noise/MersenneRandom.h"
#include "world/generator/object/TreeType.h"
#include <cstdint>

namespace cursestone::world::generator::object::trees {

class GenericTree {
public:
    virtual ~GenericTree() = default;

    // Generates the tree at (sourceX, sourceY, sourceZ). `soilId` is the
    // block directly below the trunk (sourceY - 1); it is passed in rather
    // than read from the chunk because the trunk may live in a neighbouring
    // chunk while only its overlap is written into this one. Coordinates
    // are relative to the target chunk and may fall outside 0..15 — every
    // write is clipped, which is what lets trees span chunk borders.
    // Returns false if there was no room / unsuitable soil.
    // Virtual: some species (acacia, dark oak, big oak) replace the whole
    // layout with their own algorithm.
    virtual bool generate(mcp_chunk_t* c, int sourceX, int sourceY, int sourceZ,
                          uint8_t soilId, MersenneRandom& rng);

protected:
    // Species parameters (height, log/leaf ids + data). Subclasses set
    // these in their own init() override.
    virtual void init(MersenneRandom& rng);

    // Leaf placement. Default = vanilla oak blob (radius 1..2, rounded
    // corners culled randomly). Subclasses may replace it.
    virtual void placeCanopy(mcp_chunk_t* c, int sourceX, int sourceY,
                             int sourceZ, MersenneRandom& rng);

    // Trunk placement. Default = straight vertical column.
    virtual void placeTrunk(mcp_chunk_t* c, int sourceX, int sourceY,
                            int sourceZ, MersenneRandom& rng);

    // Soil the species accepts (default: grass or dirt).
    virtual bool canPlaceOn(uint8_t soil) const;

    // Species ids. LOG/LEAVES data: 0=oak 1=spruce 2=birch 3=jungle.
    // WOOD_2/LEAVES_2 data: 0=acacia 1=dark oak.
    int     height_ = 5;
    uint8_t logId_  = 17; // LOG
    uint8_t logData_ = 0;
    uint8_t leafId_  = 18; // LEAVES
    uint8_t leafData_ = 0;

    // ---- shared block helpers (chunk-local, clipped) ----
    static bool inXZ(int v) { return v >= 0 && v < 16; }
    static bool inY(int y) { return y >= 0 && y <= 127; }
    static bool replaceable(uint8_t id);

    static void setLeaf(mcp_chunk_t* c, int x, int y, int z, uint8_t id, uint8_t data);
    static void setLog(mcp_chunk_t* c, int x, int y, int z, uint8_t id, uint8_t data);
    // Forces a block (used for the dirt under the trunk).
    static void setForced(mcp_chunk_t* c, int x, int y, int z, uint8_t id, uint8_t data);
};

} // namespace cursestone::world::generator::object::trees
