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
// Movement goals — FloatGoal, RandomStrollGoal, RandomLookAroundGoal,
// LookAtPlayerGoal.
//
// Ports of the same-named classes in
// <minecraftpe/impl/entity/ai/goals/> (MCPE 0.8.1 decompile). The era
// constructor-time holder pointer became the per-call Mob&; everything
// else keeps the era shape: control flags, canUse/canContinueToUse,
// start/stop/tick.
#pragma once
#include "entity/ai/Goal.h"
#include "entity/ai/Mob.h"

namespace cursestone::entity::ai {

/// @brief Era FloatGoal: paddle while in water or lava (flag 4 = JUMP).
class FloatGoal final : public Goal {
  public:
    explicit FloatGoal(Mob& holder) : holder_(holder) {
        setRequiredControlFlags(kControlJump);
    }

    bool canUse(Mob&) override { return holder_.isInWater() || holder_.isInLava(); }
    /// @brief The era constructor effect (navigation floats) — applied
    ///        at activation, the first start of a brain's life.
    void start(Mob&) override;
    void tick(Mob&) override;

  private:
    Mob& holder_;
};

/// @brief Era RandomStrollGoal: a 1-in-120 chance per tick to walk to a
///        random ground position (flag 3 = MOVE|LOOK).
class RandomStrollGoal final : public Goal {
  public:
    RandomStrollGoal(Mob& holder, float speedMultiplier)
        : holder_(holder), speed_multiplier_(speedMultiplier) {
        setRequiredControlFlags(kControlMove | kControlLook);
    }

    bool canUse(Mob&) override;
    bool canContinueToUse(Mob&) override;
    void start(Mob&) override;

  private:
    Mob& holder_;
    float speed_multiplier_;
    double target_x_ = 0.0, target_y_ = 0.0, target_z_ = 0.0;
};

/// @brief Era RandomLookAroundGoal: glance at a random direction (flag
///        3 = MOVE|LOOK; the look wins the head while it runs).
class RandomLookAroundGoal final : public Goal {
  public:
    explicit RandomLookAroundGoal(Mob& holder) : holder_(holder) {
        setRequiredControlFlags(kControlMove | kControlLook);
    }

    bool canUse(Mob&) override;
    bool canContinueToUse(Mob&) override { return ticks_left_ >= 0; }
    void start(Mob&) override;
    void tick(Mob&) override;

  private:
    Mob& holder_;
    double x_off_ = 0.0, z_off_ = 0.0;
    int32_t ticks_left_ = 0;
};

/// @brief Era LookAtPlayerGoal: watch the nearest player (flag 2 =
///        LOOK).
class LookAtPlayerGoal final : public Goal {
  public:
    LookAtPlayerGoal(Mob& holder, float maxDistance, float startChance = 0.02f)
        : holder_(holder), max_distance_(maxDistance),
          start_chance_(startChance) {
        setRequiredControlFlags(kControlLook);
    }

    bool canUse(Mob&) override;
    bool canContinueToUse(Mob&) override;
    void start(Mob&) override;
    void stop(Mob&) override;
    void tick(Mob&) override;

  private:
    Mob& holder_;
    float max_distance_;
    float start_chance_;
    int32_t ticks_left_ = 0;
    TargetRef target_;
};

} // namespace cursestone::entity::ai
