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
#include "entity/ai/goals/SunGoals.h"
#include "entity/ai/AiMath.h"
#include "entity/ai/PathNavigation.h"
#include "world/World.h"

namespace cursestone::entity::ai {

void RestrictSunGoal::start(Mob&) {
    if (holder_.getNavigation())
        holder_.getNavigation()->setAvoidSun(true);
}

void RestrictSunGoal::stop(Mob&) {
    if (holder_.getNavigation())
        holder_.getNavigation()->setAvoidSun(false);
}

bool FleeSunGoal::findHidePos(double& outX, double& outY, double& outZ) {
    World* level = holder_.level();
    if (!level)
        return false;
    // Era getHidePos: ten samples in a 20x6x20 box around the body; a
    // shaded cell with a low walk value wins (the walk-value term is the
    // era PathfinderMob::getWalkTargetValue, whose body is lost — the
    // port requires a solid floor instead, the practical subset).
    mcp_random_t& rng = holder_.random();
    for (int attempt = 0; attempt < 10; ++attempt) {
        const int32_t x = math::floorToInt(holder_.posX()) +
                          (int32_t)(mcp_random_next_u32(&rng) % 20u) - 10;
        const int32_t y = math::floorToInt(holder_.posY()) +
                          (int32_t)(mcp_random_next_u32(&rng) % 6u) - 3;
        const int32_t z = math::floorToInt(holder_.posZ()) +
                          (int32_t)(mcp_random_next_u32(&rng) % 20u) - 10;
        if (y < 1)
            continue;
        if (holder_.canSeeSky(x, y, z))
            continue; // The shade test.
        if (World::entityCollidable(level->getBlockId(x, y - 1, z)) ||
            World::entityCollidable(level->getBlockId(x, y, z)))
            continue; // Must stand somewhere.
        outX = (double)x;
        outY = (double)y;
        outZ = (double)z;
        return true;
    }
    return false;
}

bool FleeSunGoal::canUse(Mob&) {
    if (!holder_.isDay())
        return false;
    // Era: only a burning body flees (inert until burning exists).
    if (!holder_.isOnFire())
        return false;
    if (holder_.canSeeSky(math::floorToInt(holder_.posX()),
                          math::floorToInt(holder_.posY()),
                          math::floorToInt(holder_.posZ())))
        return false;
    if (!findHidePos(target_x_, target_y_, target_z_))
        return false;
    return true;
}

bool FleeSunGoal::canContinueToUse(Mob&) {
    return holder_.getNavigation() && !holder_.getNavigation()->isDone();
}

void FleeSunGoal::start(Mob&) {
    if (holder_.getNavigation())
        holder_.getNavigation()->moveTo(target_x_, target_y_, target_z_,
                                        speed_multiplier_);
}

} // namespace cursestone::entity::ai
