/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___/

 * CurseStone | Open Source Server Software
 *
 * Copyright (C) 2026 CurseStoneORG
 *
 * Developed by CurseStoneORG.
 * Built for an open and extensible Minecraft server ecosystem.
 *
 * Licensed under the GNU Lesser General Public License v3.0 or later.
 * You may study, modify, and redistribute this software under its license.
 *
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 * Repository: https://github.com/CurseStoneORG/CurseStone
 *
 */
// SPDX-License-Identifier: LGPL-3.0-or-later
#include "entity/ai/RandomPos.h"
#include "CurseStone.h"
#include "entity/ai/AiMath.h"
#include "entity/ai/Mob.h"
#include "world/World.h"

namespace cursestone::entity::ai {

bool generateRandomPos(double& outX, double& outY, double& outZ, Mob& mob,
                       int32_t xzRange, int32_t yRange) {
    World* level = mob.level();
    if (!level || xzRange <= 0)
        return false;
    mcp_random_t& rng = mob.random();

    for (int attempt = 0; attempt < 10; ++attempt) {
        // A random offset in [-xzRange, xzRange] x [-yRange, yRange],
        // anchored at the mob's cell.
        const int32_t dx =
            (int32_t)(mcp_random_next_u32(&rng) % (uint32_t)(2 * xzRange + 1)) -
            xzRange;
        const int32_t dy =
            (int32_t)(mcp_random_next_u32(&rng) % (uint32_t)(2 * yRange + 1)) -
            yRange;
        const int32_t dz =
            (int32_t)(mcp_random_next_u32(&rng) % (uint32_t)(2 * xzRange + 1)) -
            xzRange;
        int32_t x = math::floorToInt(mob.posX()) + dx;
        int32_t y = math::floorToInt(mob.posY()) + dy;
        int32_t z = math::floorToInt(mob.posZ()) + dz;
        if (y < 1)
            y = 1;

        // Snap down to the first solid floor within eight cells.
        int32_t drops = 0;
        while (y > 1 && !World::entityCollidable(level->getBlockId(x, y - 1, z)) &&
               drops < 8) {
            --y;
            ++drops;
        }
        if (!World::entityCollidable(level->getBlockId(x, y - 1, z)))
            continue; // Floating sample: nothing to stand on.
        // Two free body cells above the floor.
        if (World::entityCollidable(level->getBlockId(x, y, z)))
            continue;
        if (World::entityCollidable(level->getBlockId(x, y + 1, z)))
            continue;
        // Far enough to be worth the walk (one block of horizontal
        // displacement).
        const double px = (double)x + 0.5 - mob.posX();
        const double pz = (double)z + 0.5 - mob.posZ();
        if (px * px + pz * pz < 1.0)
            continue;
        outX = (double)x + 0.5;
        outY = (double)y;
        outZ = (double)z + 0.5;
        return true;
    }
    return false;
}

} // namespace cursestone::entity::ai
