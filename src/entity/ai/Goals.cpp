/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

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
#include "entity/ai/Goals.h"
#include "entity/ai/MobAI.h"
#include <cmath>

namespace cursestone::entity::ai {
namespace {

// Horizontal distance between the mob and a point, ignoring height.
double horizDist2(double ax, double az, double bx, double bz) {
    const double dx = bx - ax;
    const double dz = bz - az;
    return dx * dx + dz * dz;
}

} // namespace

// ---- WanderGoal ----

bool WanderGoal::canStart(const MobContext& ctx, MobBrain& brain) {
    if (!ctx.onGround)
        return false;
    // Start on roughly one think in twenty, so a passive mob strolls
    // about once every ten seconds and a herd spreads its walks across
    // time instead of moving in lockstep.
    return (mcp_random_next_u32(&brain.rng()) % 20u) == 0u;
}

bool WanderGoal::shouldContinue(const MobContext& ctx) const {
    if (!has_target_)
        return false;
    if (ctx.tick >= expires_tick_)
        return false;
    // Close enough to the target: the stroll is over.
    return horizDist2(ctx.x, ctx.z, target_x_, target_z_) > 0.36;
}

void WanderGoal::start(const MobContext& ctx, MobBrain& brain) {
    // 4..12 blocks in a random direction; expiration forces the goal to
    // release even when a wall stops the mob short of the point.
    const double angle = (mcp_random_next_u32(&brain.rng()) /
                          (double)UINT32_MAX) * 2.0 * 3.14159265358979323846;
    const double dist = 4.0 + (mcp_random_next_u32(&brain.rng()) /
                               (double)UINT32_MAX) * 8.0;
    target_x_ = ctx.x + std::cos(angle) * dist;
    target_z_ = ctx.z + std::sin(angle) * dist;
    expires_tick_ = ctx.tick + 100 + (mcp_random_next_u32(&brain.rng()) % 100);
    has_target_ = true;
}

void WanderGoal::tick(const MobContext& ctx, MobBrain& brain) {
    if (!has_target_)
        return;
    const double dx = target_x_ - ctx.x;
    const double dz = target_z_ - ctx.z;
    const double len = std::sqrt(dx * dx + dz * dz);
    if (len < 1e-6)
        return;
    brain.setMoveIntent(dx / len, dz / len, kWanderSpeed);
}

// ---- LookAtPlayerGoal ----

bool LookAtPlayerGoal::canStart(const MobContext& ctx, MobBrain&) {
    return ctx.playerNear && ctx.playerDist <= kStartRange;
}

bool LookAtPlayerGoal::shouldContinue(const MobContext& ctx) const {
    return ctx.playerNear && ctx.playerDist <= kStopRange;
}

void LookAtPlayerGoal::tick(const MobContext& ctx, MobBrain& brain) {
    brain.setLookIntent(faceYaw(ctx.playerX - ctx.x, ctx.playerZ - ctx.z));
}

// ---- IdleLookGoal ----

bool IdleLookGoal::canStart(const MobContext& ctx, MobBrain& brain) {
    return (mcp_random_next_u32(&brain.rng()) % 8u) == 0u;
}

bool IdleLookGoal::shouldContinue(const MobContext& ctx) const {
    return ctx.tick < expires_tick_;
}

void IdleLookGoal::start(const MobContext& ctx, MobBrain& brain) {
    // Turn at most a quarter turn from where the mob currently faces.
    const float offset =
        ((float)(mcp_random_next_u32(&brain.rng()) % 181u) - 90.0f);
    yaw_ = ctx.yaw + offset;
    expires_tick_ = ctx.tick + 20 + (mcp_random_next_u32(&brain.rng()) % 40);
}

void IdleLookGoal::tick(const MobContext&, MobBrain& brain) {
    brain.setLookIntent(yaw_);
}

} // namespace cursestone::entity::ai
