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
    // Era contract: the last attacker becomes the attack target — but
    // the era _canAttack chain refuses bodies it may not hunt, so a
    // creative player's hits draw no retaliation (vanilla keeps mobs
    // peaceful toward creative attackers).
    TargetRef attacker = holder_.getLastHurtByMob();
    if (!attacker || !attacker.attackable)
        return false;
    target_ = attacker;
    holder_.setTarget(attacker.eid, attacker.player);
    return true;
}

bool NearestAttackableTargetGoal::canUse(Mob&) {
    // Era contract (Monster::findAttackTarget): the nearest player
    // inside the follow range whose creative byte is clear — a creative
    // candidate returns an empty target, exactly like the binary's
    // `byte 0x0d2c != 0 -> reject` branch.
    TargetRef nearest = holder_.nearestPlayer(follow_range_);
    if (!nearest || !nearest.attackable)
        return false;
    target_ = nearest;
    holder_.setTarget(nearest.eid, true);
    return true;
}

} // namespace cursestone::entity::ai
