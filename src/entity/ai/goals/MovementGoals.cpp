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
#include "entity/ai/goals/MovementGoals.h"
#include "entity/ai/JumpControl.h"
#include "entity/ai/LookControl.h"
#include "entity/ai/PathNavigation.h"
#include "entity/ai/RandomPos.h"
#include <cmath>

namespace cursestone::entity::ai {

void FloatGoal::start(Mob&) {
    // Era constructor effect: the goal's mob floats.
    if (holder_.getNavigation())
        holder_.getNavigation()->setCanFloat(true);
}

void FloatGoal::tick(Mob&) {
    // Era tick(): an 80% chance per tick to paddle.
    mcp_random_t& rng = holder_.random();
    const float roll = (float)mcp_random_next_u32(&rng) * (1.0f / 4294967296.0f);
    if (roll < 0.8f && holder_.getJumpControl())
        holder_.getJumpControl()->jump();
}

bool RandomStrollGoal::canUse(Mob&) {
    // Era canUse: the idle gate, the 1/120 roll, then the sampler.
    if (holder_.getNoActionTime() > 99)
        return false;
    mcp_random_t& rng = holder_.random();
    if (mcp_random_next_u32(&rng) % 120u != 0u)
        return false;
    if (!generateRandomPos(target_x_, target_y_, target_z_, holder_, 10, 7))
        return false;
    return true;
}

bool RandomStrollGoal::canContinueToUse(Mob&) {
    return holder_.getNavigation() && !holder_.getNavigation()->isDone();
}

void RandomStrollGoal::start(Mob&) {
    if (holder_.getNavigation())
        holder_.getNavigation()->moveTo(target_x_, target_y_, target_z_,
                                        speed_multiplier_);
}

bool RandomLookAroundGoal::canUse(Mob&) {
    mcp_random_t& rng = holder_.random();
    const float roll = (float)mcp_random_next_u32(&rng) * (1.0f / 4294967296.0f);
    return roll < 0.02f;
}

void RandomLookAroundGoal::start(Mob&) {
    // Era start(): a random angle around the body, held 20..39 ticks.
    mcp_random_t& rng = holder_.random();
    const float roll = (float)mcp_random_next_u32(&rng) * (1.0f / 4294967296.0f);
    const double angle = (3.14159265358979323846 + 3.14159265358979323846) * roll;
    x_off_ = std::cos(angle);
    z_off_ = std::sin(angle);
    ticks_left_ = (int32_t)(mcp_random_next_u32(&rng) % 20u) + 20;
}

void RandomLookAroundGoal::tick(Mob&) {
    --ticks_left_;
    LookControl* look = holder_.getLookControl();
    if (!look)
        return;
    look->setLookAt(holder_.posX() + x_off_,
                    holder_.posY() + holder_.getHeadHeight(),
                    holder_.posZ() + z_off_, 10.0f,
                    holder_.getMaxHeadXRot());
}

bool LookAtPlayerGoal::canUse(Mob&) {
    // Era canUse: the start-chance roll, then the nearest player.
    mcp_random_t& rng = holder_.random();
    const float roll = (float)mcp_random_next_u32(&rng) * (1.0f / 4294967296.0f);
    if (roll >= start_chance_)
        return false;
    TargetRef nearest = holder_.nearestPlayer(max_distance_);
    if (!nearest)
        target_.clear();
    else
        target_ = nearest;
    return (bool)target_;
}

bool LookAtPlayerGoal::canContinueToUse(Mob&) {
    holder_.resolve(target_);
    if (!target_)
        return false;
    return target_.alive &&
           holder_.distanceToSqr(target_.x, target_.y, target_.z) <=
               (double)(max_distance_ * max_distance_) &&
           ticks_left_ > 0;
}

void LookAtPlayerGoal::start(Mob&) {
    mcp_random_t& rng = holder_.random();
    ticks_left_ = (int32_t)(mcp_random_next_u32(&rng) % 40u) + 40;
}

void LookAtPlayerGoal::stop(Mob&) {
    target_.clear();
}

void LookAtPlayerGoal::tick(Mob&) {
    holder_.resolve(target_);
    LookControl* look = holder_.getLookControl();
    if (!look || !target_)
        return;
    // Era tick(): the player's eye line, the era 10 deg/tick yaw speed
    // and the mob's pitch cap.
    look->setLookAt(target_.x, target_.y + target_.eyeHeight, target_.z,
                    10.0f, holder_.getMaxHeadXRot());
    --ticks_left_;
}

} // namespace cursestone::entity::ai
