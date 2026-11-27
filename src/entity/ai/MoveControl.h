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
// MoveControl — the locomotion setpoint control.
//
// Port of <minecraftpe/impl/entity/ai/MoveControl.cpp> (MCPE 0.8.1
// decompile). The navigation (or a goal directly) plants a wanted
// position with setWantedPosition; tick() turns the movement yaw toward
// it by at most 30 degrees per tick, arms the speed channel with
// speedMultiplier * baseSpeed, and jumps when the target sits just
// above the body.
#pragma once
#include <cstdint>

namespace cursestone::entity::ai {

class Mob;
class JumpControl;

class MoveControl {
  public:
    /// @brief Era constants: a below-this (squared) displacement is a
    ///        "no move wanted" and the era MAX_TURN rodata value.
    static constexpr float MIN_SPEED = 0.0005f;
    static constexpr float MIN_SPEED_SQR = MIN_SPEED * MIN_SPEED;
    static constexpr float MAX_TURN = 75.0f;
    /// @brief The per-tick yaw rotation clamp (era inline 30.0 / -30.0).
    static constexpr float MAX_YAW_STEP = 30.0f;

    explicit MoveControl(Mob* mob) : mob_(mob) {}

    bool hasWanted() const { return has_wanted_; }
    float getSpeed() const { return speed_multiplier_; }

    /// @brief Era setWantedPosition: plants the travel target.
    void setWantedPosition(double x, double y, double z, float speed);

    /// @brief Era MoveControl::tick(): rotate toward the target, arm the
    ///        speed channel, jump when the target is a step above.
    void tick();

    /// @brief Drops the wanted position (the era navigation stop path).
    void clearWanted() {
        has_wanted_ = false;
        speed_multiplier_ = 0.0f;
    }

    /// @brief The brain wires the jump control before the first tick
    ///        (the era controlledEntity->getJumpControl() link).
    void setJumpControl(JumpControl* jump) { jump_control_ = jump; }

  private:
    Mob* mob_;
    JumpControl* jump_control_ = nullptr;
    double pos_x_ = 0.0, pos_y_ = 0.0, pos_z_ = 0.0;
    float speed_multiplier_ = 0.0f;
    bool has_wanted_ = false;
};

} // namespace cursestone::entity::ai
