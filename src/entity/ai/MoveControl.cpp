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
#include "entity/ai/MoveControl.h"
#include "entity/ai/AiMath.h"
#include "entity/ai/JumpControl.h"
#include "entity/ai/Mob.h"
#include <cmath>

namespace cursestone::entity::ai {

void MoveControl::setWantedPosition(double x, double y, double z,
                                    float speed) {
    pos_x_ = x;
    pos_y_ = y;
    pos_z_ = z;
    speed_multiplier_ = speed;
    has_wanted_ = true;
}

void MoveControl::tick() {
    if (!mob_)
        return;
    Mob& mob = *mob_;

    // The era entry store: the vertical assist zeroes every tick (the
    // CurseStone actuation owns the vertical axis, so the value only
    // flows through the jump channel below).
    if (!has_wanted_)
        return;
    has_wanted_ = false;
    mob.setMoveTick(true);

    // Era: the wanted Y compares against the body-center cell, computed
    // with the same floor(minY + 0.5) shape.
    const double cellY = std::floor(mob.posY() + 0.5);
    const double dx = pos_x_ - mob.posX();
    const double dz = pos_z_ - mob.posZ();
    const double dy = pos_y_ - cellY;
    const double distSqr = dx * dx + dy * dy + dz * dz;
    if (distSqr < 0.00000025)
        return; // (0.0005)^2 — already there.

    // Era yaw steering: rotate toward the target by at most 30 degrees
    // per tick. The era computes atan2(dz, dx) - 90 against the era yaw
    // convention; the protocol conversion mirrors the sign, and the
    // symmetric clamp reproduces the same rotation in this convention.
    const float wantedYaw = math::faceYaw(dx, dz);
    mob.setYaw(math::clampRotate(mob.yaw(), wantedYaw, MAX_YAW_STEP));

    // Era speed arm: speedMultiplier * getBaseSpeed().
    mob.setSpeed(speed_multiplier_ * mob.getBaseSpeed());

    // Era step jump: a target above the body within one block of
    // horizontal distance triggers the jump control.
    if (dy > 0.0 && (dx * dx + dz * dz) < 1.0) {
        if (jump_control_)
            jump_control_->jump();
    }
}

} // namespace cursestone::entity::ai
