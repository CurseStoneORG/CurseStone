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
// Door goals — DoorInteractGoal, BreakDoorGoal.
//
// Ports of the same-named classes in
// <minecraftpe/impl/entity/ai/goals/> (MCPE 0.8.1 decompile). The era
// zombie registers BreakDoorGoal, but the era navigation's canOpenDoors
// default (off) gates the family, so the goal stays registered and inert
// unless the navigation enables doors — the port keeps both halves of
// that behavior exactly.
#pragma once
#include "entity/ai/Goal.h"
#include "entity/ai/Mob.h"

namespace cursestone::entity::ai {

/// @brief Era DoorInteractGoal(mob): detects a door cell on the current
///        path the body just collided with.
class DoorInteractGoal : public Goal {
  public:
    explicit DoorInteractGoal(Mob& holder) : holder_(holder) {}

    bool canUse(Mob&) override;
    bool canContinueToUse(Mob&) override { return !close_to_target_; }
    void start(Mob&) override;
    void tick(Mob&) override;

  protected:
    /// @brief The wooden-door id probe of the era getDoorTile.
    bool isDoorCell(int32_t x, int32_t y, int32_t z) const;

    Mob& holder_;
    int32_t door_x_ = 0, door_y_ = 0, door_z_ = 0;
    bool close_to_target_ = false;
    double target_x_ = 0.0, target_z_ = 0.0;
};

/// @brief Era BreakDoorGoal(mob): break a blocking wooden door on hard
///        difficulty (240 ticks of pounding).
class BreakDoorGoal final : public DoorInteractGoal {
  public:
    explicit BreakDoorGoal(Mob& holder) : DoorInteractGoal(holder) {}

    bool canUse(Mob&) override;
    bool canContinueToUse(Mob&) override;
    void start(Mob&) override;
    void tick(Mob&) override;

  private:
    int32_t ticks_left_ = 0;
};

} // namespace cursestone::entity::ai
