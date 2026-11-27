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
// LookControl — the head rotation control.
//
// Port of <minecraftpe/impl/entity/ai/LookControl.cpp> (MCPE 0.8.1
// decompile). Goals call setLookAt to request a facing; tick() turns
// the head toward it at the requested speeds, zeroes the pitch when no
// look target is set, and clamps the head within +/-75 degrees of the
// body while the navigation is moving.
//
// Adaptation: the era LookControl consumed its request in one tick
// (isLooking reset at the top of tick()). CurseStone ticks the goals on
// a cadence (full tier: every tick; reduced tier: every 4th), so the
// request is held for a few ticks after it lands — kRequestHoldTicks —
// and expires when the goal stops refreshing it. Without the hold, the
// head would snap back onto the body between two goal ticks.
#pragma once
#include <cstdint>

namespace cursestone::entity::ai {

class Mob;

class LookControl {
  public:
    /// @brief The tick the request stays hot after the last setLookAt.
    static constexpr int32_t kRequestHoldTicks = 4;

    explicit LookControl(Mob* mob) : mob_(mob) {}

    /// @brief Era setLookAt(x, y, z, yawSpeed, pitchSpeed).
    void setLookAt(double x, double y, double z, float yawSpeed,
                   float pitchSpeed);

    /// @brief Era setLookAt(Entity*, yawSpeed, pitchSpeed): look at a
    ///        mob's eye line (@p isMob) or at a non-mob body's middle.
    void setLookAtEntity(double x, double y, double z, bool isMob,
                         float eyeHeight, float bodyHeight, float yawSpeed,
                         float pitchSpeed);

    /// @brief Era LookControl::tick(): pitch toward the target, head
    ///        yaw toward the target (or drift back onto the body at
    ///        10 deg/tick), +/-75 clamp while navigating.
    void tick();

    bool isLooking() const { return is_looking_; }

  private:
    Mob* mob_;
    bool is_looking_ = false;
    int32_t hold_ticks_ = 0;
    float pitch_speed_ = 0.0f;
    float yaw_speed_ = 0.0f;
    double look_x_ = 0.0, look_y_ = 0.0, look_z_ = 0.0;
};

} // namespace cursestone::entity::ai
