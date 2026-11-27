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
// Target goals — TargetGoal, HurtByTargetGoal, NearestAttackableTargetGoal.
//
// The MCPE 0.8.1 decompile ships these three as empty bodies (the
// includes survive, the implementations were lost with the inline
// expansion). The era contract is known from the call sites: they run in
// the second selector (Mob::goalSelector2, ticked before the movement
// selector), claim no control flags, and write the mob's attack target
// that the combat goals read back through Mob::getTarget(). The bodies
// here reconstruct the era behavior:
//   - HurtByTargetGoal: retaliate against the last attacker.
//   - NearestAttackableTargetGoal: acquire the nearest player within the
//     follow range (16 blocks, the value every era mob registers).
// Both keep their target while it stays alive and in range, and clear it
// on stop (the era Mob cleared the attack target when the goal released).
#pragma once
#include "entity/ai/Goal.h"
#include "entity/ai/Mob.h"

namespace cursestone::entity::ai {

/// @brief Base of the era target-goal family: no flags, sets the mob's
///        attack target.
class TargetGoal : public Goal {
  public:
    explicit TargetGoal(Mob& holder, float followRange)
        : holder_(holder), follow_range_(followRange) {}

    bool canUse(Mob&) override = 0;
    bool canContinueToUse(Mob&) override;
    void stop(Mob&) override;
    bool canInterrupt() override { return true; }
    void tick(Mob&) override {}

  protected:
    Mob& holder_;
    float follow_range_;
    TargetRef target_;
};

/// @brief Era HurtByTargetGoal(mob, followRange).
class HurtByTargetGoal final : public TargetGoal {
  public:
    HurtByTargetGoal(Mob& holder, float followRange)
        : TargetGoal(holder, followRange) {}

    bool canUse(Mob&) override;
};

/// @brief Era NearestAttackableTargetGoal(mob, followRange).
class NearestAttackableTargetGoal final : public TargetGoal {
  public:
    NearestAttackableTargetGoal(Mob& holder, float followRange)
        : TargetGoal(holder, followRange) {}

    bool canUse(Mob&) override;
};

} // namespace cursestone::entity::ai
