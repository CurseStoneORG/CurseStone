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
// Passive goals — PanicGoal, TemptGoal, EatTileGoal.
//
// Ports of the same-named classes in
// <minecraftpe/impl/entity/ai/goals/> (MCPE 0.8.1 decompile).
#pragma once
#include "entity/ai/Goal.h"
#include "entity/ai/Mob.h"
#include <cstdint>
#include <vector>

namespace cursestone::entity::ai {

/// @brief Era PanicGoal(mob, speedMultiplier): run from the last hurt
///        source to a random ground position (flag 1 = MOVE).
class PanicGoal final : public Goal {
  public:
    PanicGoal(Mob& holder, float speedMultiplier)
        : holder_(holder), speed_multiplier_(speedMultiplier) {
        setRequiredControlFlags(kControlMove);
    }

    bool canUse(Mob&) override;
    bool canContinueToUse(Mob&) override;
    void start(Mob&) override;

  private:
    Mob& holder_;
    float speed_multiplier_;
    double target_x_ = 0.0, target_y_ = 0.0, target_z_ = 0.0;
};

/// @brief Era TemptGoal(mob, speedMultiplier, followItemIds,
///        scaredOfPlayerMovement): follow a player holding the tempting
///        item (flag 3 = MOVE|LOOK).
class TemptGoal final : public Goal {
  public:
    TemptGoal(Mob& holder, float speedMultiplier,
              std::initializer_list<uint16_t> followItemIds,
              bool scaredOfMovement)
        : holder_(holder), speed_multiplier_(speedMultiplier),
          follow_item_ids_(followItemIds),
          scared_of_movement_(scaredOfMovement) {
        setRequiredControlFlags(kControlMove | kControlLook);
    }

    bool isRunning() const { return running_; }
    bool canUse(Mob&) override;
    bool canContinueToUse(Mob&) override;
    void start(Mob&) override;
    void stop(Mob&) override;
    void tick(Mob&) override;

  private:
    Mob& holder_;
    float speed_multiplier_;
    std::vector<uint16_t> follow_item_ids_;
    bool scared_of_movement_;
    TargetRef followed_;
    bool running_ = false;
    int32_t timeout_ = 0;
    int32_t repath_ticks_ = 0;
    double last_px_ = 0.0, last_py_ = 0.0, last_pz_ = 0.0;
    float last_pitch_ = 0.0f, last_yaw_ = 0.0f;
};

/// @brief Era EatTileGoal(mob): the sheep grass-eating behavior (flag 7
///        = MOVE|LOOK|JUMP — nothing else may move the body).
class EatTileGoal final : public Goal {
  public:
    explicit EatTileGoal(Mob& holder) : holder_(holder) {
        setRequiredControlFlags(kControlMove | kControlLook | kControlJump);
    }

    bool canUse(Mob&) override;
    bool canContinueToUse(Mob&) override { return ticks_left_ > 0; }
    void start(Mob&) override;
    void stop(Mob&) override;
    void tick(Mob&) override;

  private:
    Mob& holder_;
    int32_t ticks_left_ = 0;
};

} // namespace cursestone::entity::ai
