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
#include "entity/ai/goals/TargetGoals.h"
#include <cmath>

namespace cursestone::entity::ai {

bool TargetGoal::canContinueToUse(Mob& mob) {
    // The era Goal::canContinueToUse default delegates to canUse(), so a
    // running target goal re-resolves its target every tick — a fresh
    // retaliation stamp overwrites a stale acquire in the same pass.
    return canUse(mob);
}

void TargetGoal::stop(Mob&) {
    // The era release cleared the attack target with the goal.
    target_.clear();
    holder_.setTarget(0, false);
}

bool HurtByTargetGoal::canUse(Mob&) {
    // Era contract: the last attacker becomes the attack target.
    TargetRef attacker = holder_.getLastHurtByMob();
    if (!attacker)
        return false;
    target_ = attacker;
    holder_.setTarget(attacker.eid, attacker.player);
    return true;
}

bool NearestAttackableTargetGoal::canUse(Mob&) {
    // Era contract: the nearest player inside the follow range.
    TargetRef nearest = holder_.nearestPlayer(follow_range_);
    if (!nearest)
        return false;
    target_ = nearest;
    holder_.setTarget(nearest.eid, true);
    return true;
}

} // namespace cursestone::entity::ai
