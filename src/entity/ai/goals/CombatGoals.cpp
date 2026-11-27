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
#include "entity/ai/goals/CombatGoals.h"
#include "entity/ai/LookControl.h"
#include "entity/ai/PathNavigation.h"
#include "entity/ai/Sensing.h"
#include <cmath>

namespace cursestone::entity::ai {

// ---- MeleeAttackGoal ----

bool MeleeAttackGoal::canUse(Mob&) {
    target_ = holder_.getTarget();
    if (!target_)
        return false;
    // Era canUse: a path to the target gates the start (the navigation
    // adopts it and the movement feeds from it).
    if (!holder_.getNavigation())
        return false;
    return holder_.getNavigation()->moveToEntity(target_, speed_multiplier_);
}

bool MeleeAttackGoal::canContinueToUse(Mob&) {
    if (!holder_.getTarget())
        return false;
    holder_.resolve(target_);
    if (!target_ || !target_.alive)
        return false;
    // Era: the must-see variant keeps running regardless of the path;
    // otherwise the goal runs while the navigation still walks.
    return must_see_ || !holder_.getNavigation()->isDone();
}

void MeleeAttackGoal::stop(Mob&) {
    target_.clear();
    if (holder_.getNavigation())
        holder_.getNavigation()->stop();
}

void MeleeAttackGoal::tick(Mob&) {
    holder_.resolve(target_);
    if (!target_)
        return;
    LookControl* look = holder_.getLookControl();
    if (look)
        look->setLookAt(target_.x, target_.y + target_.eyeHeight, target_.z,
                        30.0f, 30.0f);

    PathNavigation* nav = holder_.getNavigation();
    if (nav) {
        // Era: with sight, re-path toward the target every 4..10 ticks.
        const bool canChase = must_see_ || holder_.getSensing()->canSee(
                                                 target_.eid,
                                                 target_.x,
                                                 target_.y + target_.eyeHeight,
                                                 target_.z);
        if (canChase && --repath_ticks_ <= 0) {
            mcp_random_t& rng = holder_.random();
            repath_ticks_ = (int32_t)(mcp_random_next_u32(&rng) % 7u) + 4;
            nav->moveToEntity(target_, speed_multiplier_);
        }
    }

    // Era attack window: inside (width * 2)^2 of the victim's box
    // bottom, one hit per 20 ticks.
    if (attack_cooldown_ > 0)
        --attack_cooldown_;
    const double reach = (double)holder_.entityWidth() * 2.0;
    if (holder_.distanceToSqr(target_.x, target_.y, target_.z) <=
            reach * reach &&
        attack_cooldown_ <= 0) {
        attack_cooldown_ = 20;
        holder_.doHurtTarget(target_);
    }
}

// ---- ArrowAttackGoal ----

bool ArrowAttackGoal::canUse(Mob&) {
    target_ = holder_.getTarget();
    return (bool)target_;
}

bool ArrowAttackGoal::canContinueToUse(Mob&) {
    // Era: refresh the target through canUse; without one, keep walking
    // the current path.
    if (canUse(holder_))
        return true;
    return holder_.getNavigation() && !holder_.getNavigation()->isDone();
}

void ArrowAttackGoal::stop(Mob&) {
    target_.clear();
}

void ArrowAttackGoal::tick(Mob&) {
    holder_.resolve(target_);
    if (!target_ || !holder_.getNavigation())
        return;

    // Era: the sight streak and the square of the 10-block range gate
    // the hold-and-shoot behavior.
    const double distSqr =
        holder_.distanceToSqr(target_.x, target_.y, target_.z);
    const bool canSee =
        holder_.getSensing()->canSee(target_.eid, target_.x,
                                     target_.y + target_.eyeHeight,
                                     target_.z);
    if (canSee)
        ++seen_ticks_;
    else
        seen_ticks_ = 0;

    if (distSqr <= 100.0 && seen_ticks_ > 19) {
        holder_.getNavigation()->stop();
    } else {
        holder_.getNavigation()->moveToEntity(target_, speed_multiplier_);
    }

    LookControl* look = holder_.getLookControl();
    if (look)
        look->setLookAt(target_.x, target_.y + target_.eyeHeight, target_.z,
                        30.0f, 30.0f);

    if (attack_timeout_ > 0)
        --attack_timeout_;
    if (attack_timeout_ == 0 && distSqr <= 100.0 && canSee) {
        // Era fireAtTarget: attack type 1 launches the arrow (the only
        // armed type on this server; type 2 was the snowball variant).
        if (attack_type_ == 1)
            holder_.fireArrowAt(target_);
        attack_timeout_ = delay_between_attack_;
    }
}

// ---- SwellGoal ----

bool SwellGoal::canUse(Mob&) {
    // Era: an already-swelling creeper keeps the goal; otherwise the
    // target must be within three blocks.
    if (holder_.getSwellDir() > 0)
        return true;
    target_ = holder_.getTarget();
    if (!target_)
        return false;
    return holder_.distanceToSqr(target_.x, target_.y, target_.z) < 9.0;
}

void SwellGoal::start(Mob&) {
    if (holder_.getNavigation())
        holder_.getNavigation()->stop();
    target_ = holder_.getTarget();
}

void SwellGoal::stop(Mob&) {
    target_.clear();
}

void SwellGoal::tick(Mob&) {
    holder_.resolve(target_);
    // Era tick(): swell while the target cannot be reached or seen,
    // calm down while it can.
    int32_t dir = -1;
    if (target_) {
        if (holder_.distanceToSqr(target_.x, target_.y, target_.z) > 49.0 ||
            !holder_.getSensing()->canSee(target_.eid, target_.x,
                                          target_.y + target_.eyeHeight,
                                          target_.z)) {
            dir = 1;
        }
    }
    holder_.setSwellDir(dir);
}

} // namespace cursestone::entity::ai
