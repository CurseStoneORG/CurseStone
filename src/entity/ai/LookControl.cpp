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
#include "entity/ai/LookControl.h"
#include "entity/ai/AiMath.h"
#include "entity/ai/Mob.h"
#include "entity/ai/PathNavigation.h"
#include <cmath>

namespace cursestone::entity::ai {

void LookControl::setLookAt(double x, double y, double z, float yawSpeed,
                            float pitchSpeed) {
    look_x_ = x;
    look_y_ = y;
    look_z_ = z;
    yaw_speed_ = yawSpeed;
    pitch_speed_ = pitchSpeed;
    is_looking_ = true;
    hold_ticks_ = kRequestHoldTicks;
}

void LookControl::setLookAtEntity(double x, double y, double z, bool isMob,
                                  float eyeHeight, float bodyHeight,
                                  float yawSpeed, float pitchSpeed) {
    // Era setLookAt(Entity*): mobs are looked at eye-high, non-mobs at
    // their bounding-box middle.
    const double targetY =
        isMob ? (y + eyeHeight) : (y + bodyHeight * 0.5);
    setLookAt(x, targetY, z, yawSpeed, pitchSpeed);
}

void LookControl::tick() {
    if (!mob_)
        return;
    Mob& mob = *mob_;

    mob.setPitch(0.0f);
    float wantedYaw;
    if (is_looking_) {
        if (hold_ticks_ > 0)
            --hold_ticks_;
        const double dx = look_x_ - mob.posX();
        const double dz = look_z_ - mob.posZ();
        const double horiz = std::sqrt(dz * dz + dx * dx);
        // Era pitch: atan2(vertical, horizontal), applied negated so a
        // downward look lands as a positive pitch (protocol-84 pitch is
        // positive = down).
        const double vertical = look_y_ - (mob.posY() + mob.getHeadHeight());
        const float pitchDeg =
            (float)(std::atan2(vertical, horiz) * (180.0 / 3.14159265358979323846));
        mob.setPitch(math::clampRotate(mob.pitch(), -pitchDeg, pitch_speed_));
        // Era head yaw target: atan2(dz, dx) - 90 — the client convention
        // itself (the era formula and faceYaw agree; see AiMath.h).
        wantedYaw = math::faceYaw(dx, dz);
    } else {
        // No request: the head drifts back onto the body yaw at the
        // era's fixed 10 deg/tick.
        wantedYaw = mob.bodyYaw();
        yaw_speed_ = 10.0f;
    }
    if (hold_ticks_ <= 0)
        is_looking_ = false;

    mob.setHeadYaw(math::clampRotate(mob.headYaw(), wantedYaw, yaw_speed_));

    // Era tail: while the navigation carries a path, the head may never
    // lead or trail the body by more than 75 degrees.
    float headYaw = mob.headYaw();
    const float offset = math::wrapDegrees(headYaw - mob.bodyYaw());
    const PathNavigation* nav = mob.getNavigation();
    if (nav && !nav->isDone()) {
        if (offset < -75.0f)
            headYaw = mob.bodyYaw() - 75.0f;
        else if (offset > 75.0f)
            headYaw = mob.bodyYaw() + 75.0f;
        mob.setHeadYaw(headYaw);
    }
}

} // namespace cursestone::entity::ai
