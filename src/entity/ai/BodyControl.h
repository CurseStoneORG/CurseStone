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
// BodyControl — the body rotation reconciler.
//
// Port of <minecraftpe/impl/entity/ai/BodyControl.cpp> (MCPE 0.8.1
// decompile, BodyControl::clientTick). Two paths:
//   - idle (the body moved less than MoveControl::MIN_SPEED since last
//     tick): the body yaw chases the head yaw under a shrinking clamp —
//     after 10 still ticks the allowed offset ramps down from 75 to 0
//     degrees, so the body squares up onto the head.
//   - moving: the body yaw chases the movement yaw at 10 deg/tick and
//     the head is pulled back inside the +/-75 cone.
#pragma once
#include <cstdint>

namespace cursestone::entity::ai {

class Mob;

class BodyControl {
  public:
    /// @brief Era BodyControl::maxClampAngle (rodata 75.0): the head
    ///        yaw may never lead or trail the body by more than this.
    static constexpr float MAX_CLAMP_ANGLE = 75.0f;

    explicit BodyControl(Mob* mob) : mob_(mob) {}

    /// @brief Era clientTick (the server runs the same reconciler).
    void tick();

  private:
    Mob* mob_;
    int32_t idle_ticks_ = 0;   ///< Era field_4.
    float head_anchor_ = 0.0f; ///< Era field_8.
};

} // namespace cursestone::entity::ai
