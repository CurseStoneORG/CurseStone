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
// Sun goals — RestrictSunGoal, FleeSunGoal.
//
// Ports of the same-named classes in
// <minecraftpe/impl/entity/ai/goals/> (MCPE 0.8.1 decompile). These are
// the skeleton's daylight behaviors: pathing prefers shade while the sun
// is up, and a burning body runs for cover (the burn branch stays inert
// until mob burning exists on this server).
#pragma once
#include "entity/ai/Goal.h"
#include "entity/ai/Mob.h"

namespace cursestone::entity::ai {

/// @brief Era RestrictSunGoal(mob): paths avoid sun during the day (no
///        control flags — the era ctor never set any).
class RestrictSunGoal final : public Goal {
  public:
    explicit RestrictSunGoal(Mob& holder) : holder_(holder) {}

    bool canUse(Mob&) override { return holder_.isDay(); }
    void start(Mob&) override;
    void stop(Mob&) override;
    void tick(Mob&) override {}

  private:
    Mob& holder_;
};

/// @brief Era FleeSunGoal(mob, speedMultiplier): a burning body runs to
///        shade (flag 1 = MOVE).
class FleeSunGoal final : public Goal {
  public:
    FleeSunGoal(Mob& holder, float speedMultiplier)
        : holder_(holder), speed_multiplier_(speedMultiplier) {
        setRequiredControlFlags(kControlMove);
    }

    bool canUse(Mob&) override;
    bool canContinueToUse(Mob&) override;
    void start(Mob&) override;
    void tick(Mob&) override {}

  private:
    /// @brief Era getHidePos: ten random samples for a shaded, low-value
    ///        ground cell.
    bool findHidePos(double& outX, double& outY, double& outZ);

    Mob& holder_;
    float speed_multiplier_;
    double target_x_ = 0.0, target_y_ = 0.0, target_z_ = 0.0;
};

} // namespace cursestone::entity::ai
