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
#include "BigOakTree.h"
#include <cstdlib>

namespace cursestone::world::generator::object::trees {

void BigOakTree::init(MersenneRandom& rng) {
    height_ = 5 + (int)rng.nextInt(12); // 5..16 (reference BigOakTree)
    trunkHeight_ = (int)(height_ * 0.618);
    if (trunkHeight_ >= height_)
        trunkHeight_ = height_ - 1;
    if (trunkHeight_ < 2)
        trunkHeight_ = 2;
    logId_ = 17;
    logData_ = 0; // LOG, oak
    leafId_ = 18;
    leafData_ = 0; // LEAVES, oak
}

void BigOakTree::placeTrunk(mcp_chunk_t* c, int sourceX, int sourceY, int sourceZ,
                            MersenneRandom&) {
    for (int i = 0; i < trunkHeight_; ++i)
        setLog(c, sourceX, sourceY + i, sourceZ, logId_, logData_);
}

void BigOakTree::placeCanopy(mcp_chunk_t* c, int sourceX, int sourceY, int sourceZ,
                             MersenneRandom& rng) {
    const int top = sourceY + height_;

    // Main canopy blob (rounded, 3 layers + a small cap).
    for (int ly = top - 2; ly <= top + 1; ++ly) {
        int r = (ly <= top) ? 2 : 1;
        for (int dx = -r; dx <= r; ++dx) {
            for (int dz = -r; dz <= r; ++dz) {
                if (std::abs(dx) != r || std::abs(dz) != r || rng.nextBoolean())
                    setLeaf(c, sourceX + dx, ly, sourceZ + dz, leafId_, leafData_);
            }
        }
    }

    // Branches growing diagonally out of the trunk, each capped with a
    // leaf cluster — this is what makes the "fancy" big oak silhouette.
    int branches = 2 + (int)rng.nextInt(3);
    for (int b = 0; b < branches; ++b) {
        int dirX = (int)rng.nextInt(3) - 1;
        int dirZ = (int)rng.nextInt(3) - 1;
        if (dirX == 0 && dirZ == 0)
            dirX = 1;

        int startY = sourceY + trunkHeight_ - 1 + (int)rng.nextInt(2);
        int len = 2 + (int)rng.nextInt(3);
        int bx = sourceX, bz = sourceZ, by = startY;
        for (int k = 0; k < len; ++k) {
            bx += dirX;
            bz += dirZ;
            by += 1;
            setLog(c, bx, by, bz, logId_, logData_);
        }

        int r = 1 + (int)rng.nextInt(2);
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -r; dx <= r; ++dx)
                for (int dz = -r; dz <= r; ++dz)
                    if (dx * dx + dz * dz <= r * r + 1)
                        setLeaf(c, bx + dx, by + dy, bz + dz, leafId_, leafData_);
    }
}

} // namespace cursestone::world::generator::object::trees
