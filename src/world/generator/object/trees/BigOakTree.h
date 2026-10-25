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
 */
// SPDX-License-Identifier: LGPL-3.0-or-later
// BigOakTree — the vanilla FANCY oak (feature "fancy_oak"), ported 1:1 from
// the reference implementation (FancyTrunkPlacer + FancyFoliagePlacer) and
// adapted to this generator's single-chunk clipped writer.
//
// The shape follows the vanilla tree:
//   * trunk rising to floor(height * 0.618), height = 5..16,
//   * one branch cluster PER trunk level above 0.3 * height: each cluster
//     picks a polar offset (radius from the treeShape profile, random
//     angle), verifies the branch path is clear, and records a foliage
//     attachment whose branch base slides down the trunk by BRANCH_SLOPE,
//   * every attachment gets the vanilla blob: 5 leaf layers (offset 4 down
//     to 0), radius 2 at the top and bottom rows and 3 in between, cut by
//     the circular (dx+0.5)^2 + (dz+0.5)^2 <= r^2 test — no random corner
//     culling, which is why the vanilla crown reads as round.
#pragma once
#include "GenericTree.h"
#include <vector>

namespace cursestone::world::generator::object::trees {

class BigOakTree : public GenericTree {
  public:
    // The fancy oak interleaves trunk, branches and canopy, so it replaces
    // the whole generate() flow instead of just the two place* hooks.
    bool generate(mcp_chunk_t* c, int sourceX, int sourceY, int sourceZ, uint8_t soilId,
                  MersenneRandom& rng) override;

  protected:
    void init(MersenneRandom& rng) override;

  private:
    // One foliage attachment: where the leaf blob sits and where its branch
    // meets the trunk (the Y the branch grows out of).
    struct FoliageCoord {
        int x, y, z;
        int branchBase;
    };

    int trunkHeight_ = 5;

    // FancyTrunkPlacer::treeShape — crown radius profile at relY; negative
    // means "no branch below this level".
    static double treeShape(int height, int relY);
    // FancyTrunkPlacer::trimBranches — branches below 0.2 * height are cut.
    static bool trimBranches(int height, int localY) {
        return (double)localY >= (double)height * 0.2;
    }

    // makeLimb: walk a straight line placing logs (place=true) or checking
    // that every block is replaceable (place=false, the vanilla probe run).
    bool limb(mcp_chunk_t* c, int sx, int sy, int sz, int ex, int ey, int ez, bool place) const;
    // isFree in the reference: air/replaceable, or outside this chunk (which
    // this writer cannot see and therefore treats as clear).
    bool freeAt(mcp_chunk_t* c, int x, int y, int z) const;

    // FancyFoliagePlacer (radius 2, offset 4, height 4): five layers, r=2 at
    // the ends and r=3 in the middle, circular cut, into replaceable blocks.
    void foliageBlob(mcp_chunk_t* c, int fx, int fy, int fz) const;

    std::vector<FoliageCoord> coords_;
};

} // namespace cursestone::world::generator::object::trees
