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
#include "BigOakTree.h"
#include "block/BlockIds.h"
#include <cmath>
#include <cstdlib>

namespace cursestone::world::generator::object::trees {

// Vanilla FancyTrunkPlacer constants.
static constexpr double kTrunkHeightScale = 0.618;
static constexpr double kBranchSlope = 0.381;
static constexpr double kBranchLengthMagic = 0.328;
// FancyFoliagePlacer(radius=2, offset=4, height=4).
static constexpr int kFoliageRadius = 2;
static constexpr int kFoliageOffset = 4;
static constexpr int kFoliageHeight = 4;

void BigOakTree::init(MersenneRandom& rng) {
    // TrunkPlacer::getTreeHeight with FancyTrunkPlacer(3, 11, 0) gives a
    // treeHeight of 3..14 and the fancy placer then builds with
    // height = treeHeight + 2  ->  5..16 (same spread the old code had).
    height_ = 5 + (int)rng.nextInt(12);
    trunkHeight_ = (int)((double)height_ * kTrunkHeightScale);
    if (trunkHeight_ >= height_)
        trunkHeight_ = height_ - 1;
    if (trunkHeight_ < 2)
        trunkHeight_ = 2;
    logId_ = 17;
    logData_ = 0; // LOG, oak
    leafId_ = 18;
    leafData_ = 0; // LEAVES, oak
}

double BigOakTree::treeShape(int height, int relY) {
    // FancyTrunkPlacer::treeShape, verbatim. The radius of the crown at
    // relY: negative below 0.3 * height (no branches there), 0 past the
    // crown, and a circle profile in between.
    if ((double)relY < (double)height * 0.3)
        return -1.0;
    const double radius = (double)height / 2.0;
    const double adjacent = radius - (double)relY;
    if (adjacent == 0.0)
        return radius * 0.5;
    if (std::fabs(adjacent) >= radius)
        return 0.0;
    return std::sqrt(radius * radius - adjacent * adjacent) * 0.5;
}

bool BigOakTree::freeAt(mcp_chunk_t* c, int x, int y, int z) const {
    if (!inXZ(x) || !inXZ(z) || !inY(y))
        return true; // outside this chunk: cannot check, assume clear
    return replaceable(mcp_chunk_get_block_id(c, x, y, z));
}

bool BigOakTree::limb(mcp_chunk_t* c, int sx, int sy, int sz, int ex, int ey, int ez,
                      bool place) const {
    // The reference bails "free" when probing a zero-length limb.
    if (!place && sx == ex && sy == ey && sz == ez)
        return true;
    const int dx = ex - sx, dy = ey - sy, dz = ez - sz;
    const int steps = std::max(std::max(std::abs(dx), std::abs(dy)), std::abs(dz));
    if (steps == 0)
        return place ? (setLog(c, sx, sy, sz, logId_, logData_), true)
                     : freeAt(c, sx, sy, sz);
    const double fx = (double)dx / (double)steps;
    const double fy = (double)dy / (double)steps;
    const double fz = (double)dz / (double)steps;
    for (int i = 0; i <= steps; ++i) {
        const int bx = sx + (int)std::floor(0.5 + (double)i * fx);
        const int by = sy + (int)std::floor(0.5 + (double)i * fy);
        const int bz = sz + (int)std::floor(0.5 + (double)i * fz);
        if (place) {
            setLog(c, bx, by, bz, logId_, logData_);
        } else if (!freeAt(c, bx, by, bz)) {
            return false;
        }
    }
    return true;
}

void BigOakTree::foliageBlob(mcp_chunk_t* c, int fx, int fy, int fz) const {
    // FancyFoliagePlacer::createFoliage: layers from offset (4) down to
    // offset - height (0). The end rows keep the placer's radius (2); the
    // middle rows grow by one (3). The circular skip test
    // (dx+0.5)^2 + (dz+0.5)^2 > r^2 is what rounds the crown — there is no
    // random corner culling in the fancy oak.
    for (int yo = kFoliageOffset; yo >= kFoliageOffset - kFoliageHeight; --yo) {
        const int r = kFoliageRadius +
                      ((yo != kFoliageOffset && yo != kFoliageOffset - kFoliageHeight) ? 1 : 0);
        const double rr = (double)(r * r);
        for (int dx = -r; dx <= r; ++dx) {
            for (int dz = -r; dz <= r; ++dz) {
                const double ddx = (double)dx + 0.5;
                const double ddz = (double)dz + 0.5;
                if (ddx * ddx + ddz * ddz > rr)
                    continue; // outside the circle
                setLeaf(c, fx + dx, fy + yo, fz + dz, leafId_, leafData_);
            }
        }
    }
}

bool BigOakTree::generate(mcp_chunk_t* c, int sourceX, int sourceY, int sourceZ, uint8_t soilId,
                          MersenneRandom& rng) {
    init(rng);

    if (sourceY < 2 || sourceY > 124)
        return false;
    if (!canPlaceOn(soilId))
        return false;

    const int height = height_;
    const int trunkTop = sourceY + trunkHeight_;

    // ---- FancyTrunkPlacer::placeTrunk ------------------------------------
    // The crown-top attachment first (branchBase = the trunk top), then one
    // branch cluster per level down to the ground. clustersPerY collapses
    // to exactly 1 for every height this tree can roll
    // (min(1, floor(1.382 + (height/13)^2)) = 1), so one cluster per level.
    coords_.clear();
    coords_.push_back({sourceX, sourceY + (height - 5), sourceZ, trunkTop});

    for (int relY = height - 5; relY >= 0; --relY) {
        const double shape = treeShape(height, relY);
        if (shape < 0.0)
            continue;

        // Polar offset of the branch tip, exactly like the reference.
        const double radius = shape * ((double)rng.nextFloat() + kBranchLengthMagic);
        const double angle = (double)rng.nextFloat() * 6.283185307179586;
        const double fx = radius * std::sin(angle) + 0.5;
        const double fz = radius * std::cos(angle) + 0.5;

        const int sx = sourceX + (int)std::floor(fx);
        const int sy = sourceY + relY - 1;
        const int sz = sourceZ + (int)std::floor(fz);

        // Probe 1: a clear vertical run of 5 above the branch tip.
        if (!limb(c, sx, sy, sz, sx, sy + 5, sz, false))
            continue;

        // The branch base slides DOWN the trunk by BRANCH_SLOPE per block
        // of horizontal distance, never above the trunk top.
        const int dx = sourceX - sx;
        const int dz = sourceZ - sz;
        const double branchHeight =
            (double)sy - std::sqrt((double)(dx * dx + dz * dz)) * kBranchSlope;
        const int branchTop =
            branchHeight > (double)trunkTop ? trunkTop : (int)branchHeight;

        // Probe 2: a clear path from the trunk out to the tip.
        if (!limb(c, sourceX, branchTop, sourceZ, sx, sy, sz, false))
            continue;

        coords_.push_back({sx, sy, sz, branchTop});
    }

    // Main trunk, then the branches and their foliage. A limb is drawn from
    // (x, branchBase, z) to the attachment UNLESS they are the same block
    // (the crown-top attachment sits on the trunk itself); branches below
    // 0.2 * height are trimmed, and only the survivors grow leaves.
    limb(c, sourceX, sourceY, sourceZ, sourceX, trunkTop, sourceZ, true);
    for (const FoliageCoord& fc : coords_) {
        const bool onTrunk = fc.x == sourceX && fc.z == sourceZ && fc.y == fc.branchBase;
        if (!trimBranches(height, fc.branchBase - sourceY))
            continue;
        if (!onTrunk)
            limb(c, sourceX, fc.branchBase, sourceZ, fc.x, fc.y, fc.z, true);
        foliageBlob(c, fc.x, fc.y, fc.z);
    }

    // The block right under the trunk always becomes dirt (reference).
    if (inXZ(sourceX) && inXZ(sourceZ)) {
        const uint8_t below = mcp_chunk_get_block_id(c, sourceX, sourceY - 1, sourceZ);
        if (below == (uint8_t)cursestone::block::BlockId::GRASS ||
            below == (uint8_t)cursestone::block::BlockId::DIRT)
            setForced(c, sourceX, sourceY - 1, sourceZ, (uint8_t)cursestone::block::BlockId::DIRT,
                      0);
    }
    return true;
}

} // namespace cursestone::world::generator::object::trees
