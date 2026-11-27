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
#include "entity/ai/BodyControl.h"
#include "entity/ai/AiMath.h"
#include "entity/ai/MoveControl.h"
#include "entity/ai/Mob.h"
#include <cmath>

namespace cursestone::entity::ai {

void BodyControl::tick() {
    if (!mob_)
        return;
    Mob& mob = *mob_;

    // Era moving test: the displacement since the stored previous
    // position is at or below MoveControl::MIN_SPEED squared.
    const double dx = mob.posX() - mob.prevX();
    const double dz = mob.posZ() - mob.prevZ();
    const bool idle =
        !mob.prevValid() || (dx * dx + dz * dz) <= MoveControl::MIN_SPEED_SQR;

    if (idle) {
        float clamp = MAX_CLAMP_ANGLE;
        const float headTurn = std::fabs(mob.headYaw() - head_anchor_);
        if (headTurn <= 15.0f) {
            // The head kept still: count the idle ticks; past 10, the
            // allowed head-to-body offset ramps down (era shape
            // 1.0 - (count - 9) / 10, clamped at 0).
            ++idle_ticks_;
            if (idle_ticks_ > 10) {
                float ramp = 1.0f - (float)(idle_ticks_ - 10) / 10.0f;
                if (ramp < 0.0f)
                    ramp = 0.0f;
                clamp = ramp * MAX_CLAMP_ANGLE;
            }
        } else {
            idle_ticks_ = 0;
            head_anchor_ = mob.headYaw();
        }
        // Era idle path: the body chases the head under the clamp; the
        // movement yaw follows the body.
        const float headYaw = mob.headYaw();
        const float offset = math::wrapDegrees(headYaw - mob.bodyYaw());
        const float clamped =
            offset > clamp ? clamp : (offset < -clamp ? -clamp : offset);
        const float bodyYaw = headYaw - clamped;
        mob.setBodyYaw(bodyYaw);
        mob.setYaw(bodyYaw);
        return;
    }

    // Era moving path: the body chases the movement yaw at 10 deg/tick
    // and the head is pulled back inside the +/-75 cone.
    const float bodyYaw = mob.bodyYaw();
    const float offset = math::wrapDegrees(mob.yaw() - bodyYaw);
    const float step = offset > 10.0f ? 10.0f : (offset < -10.0f ? -10.0f : offset);
    const float newBody = bodyYaw + step;
    mob.setBodyYaw(newBody);
    float headOffset = math::wrapDegrees(newBody - mob.headYaw());
    if (headOffset < -MAX_CLAMP_ANGLE)
        headOffset = -MAX_CLAMP_ANGLE;
    else if (headOffset >= MAX_CLAMP_ANGLE)
        headOffset = MAX_CLAMP_ANGLE;
    mob.setHeadYaw(newBody - headOffset);
    head_anchor_ = mob.headYaw();
    idle_ticks_ = 0;
}

} // namespace cursestone::entity::ai
