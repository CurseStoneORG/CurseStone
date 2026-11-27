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
// Combat goals — MeleeAttackGoal, ArrowAttackGoal, SwellGoal.
//
// Ports of the same-named classes in
// <minecraftpe/impl/entity/ai/goals/> (MCPE 0.8.1 decompile).
#pragma once
#include "entity/ai/Goal.h"
#include "entity/ai/Mob.h"

namespace cursestone::entity::ai {

/// @brief Era MeleeAttackGoal(mob, speedMultiplier, mustSee): chase the
///        attack target and melee it (flag 3 = MOVE|LOOK).
class MeleeAttackGoal final : public Goal {
  public:
    MeleeAttackGoal(Mob& holder, float speedMultiplier, bool mustSee)
        : holder_(holder), speed_multiplier_(speedMultiplier), must_see_(mustSee) {
        setRequiredControlFlags(kControlMove | kControlLook);
    }

    bool canUse(Mob&) override;
    bool canContinueToUse(Mob&) override;
    void stop(Mob&) override;
    void tick(Mob&) override;

  private:
    Mob& holder_;
    float speed_multiplier_;
    bool must_see_;
    TargetRef target_;
    int32_t attack_cooldown_ = 0; ///< Era field_24.
    int32_t repath_ticks_ = 0;    ///< Era field_34.
};

/// @brief Era ArrowAttackGoal(mob, speedMultiplier, attackType,
///        delayBetweenAttack): keep the range, shoot (flag 3).
class ArrowAttackGoal final : public Goal {
  public:
    ArrowAttackGoal(Mob& holder, float speedMultiplier, int32_t attackType,
                    int32_t delayBetweenAttack)
        : holder_(holder), speed_multiplier_(speedMultiplier),
          attack_type_(attackType), delay_between_attack_(delayBetweenAttack) {
        setRequiredControlFlags(kControlMove | kControlLook);
    }

    bool canUse(Mob&) override;
    bool canContinueToUse(Mob&) override;
    void stop(Mob&) override;
    void tick(Mob&) override;

  private:
    Mob& holder_;
    float speed_multiplier_;
    int32_t attack_type_;
    int32_t delay_between_attack_;
    TargetRef target_;
    int32_t attack_timeout_ = 0;
    int32_t seen_ticks_ = 0;
};

/// @brief Era SwellGoal(mob): the creeper fuse driver (flag 1 = MOVE).
class SwellGoal final : public Goal {
  public:
    explicit SwellGoal(Mob& holder) : holder_(holder) {
        setRequiredControlFlags(kControlMove);
    }

    bool canUse(Mob&) override;
    void start(Mob&) override;
    void stop(Mob&) override;
    void tick(Mob&) override;

  private:
    Mob& holder_;
    TargetRef target_;
};

} // namespace cursestone::entity::ai
