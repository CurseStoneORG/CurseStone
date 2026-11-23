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

/// @brief The player eye line the look pitch aims at (the era entity
///        model's 1.62); the mob side of the pivot is the species eye
///        height from the context profile.
constexpr double kPlayerEyeHeight = 1.62;

} // namespace

// ---- WanderGoal ----

bool WanderGoal::canStart(const MobContext& ctx, MobBrain& brain) {
    // Land mobs stroll from solid ground; swimming mobs paddle from the
    // water too — without the water branch a mob that enters a pond
    // would never stroll again (onGround is false while it swims) and
    // would just drift with the buoyancy.
    if (!ctx.onGround && !ctx.inWater)
        return false;
    // Start on roughly one think in twelve, so a passive mob strolls
    // about once every six seconds and a herd spreads its walks across
    // time instead of moving in lockstep. The old one-in-twenty made
    // mobs stand around for ten seconds between legs — most of the
    // "AI feels slow" read came from that idle share.
    return (mcp_random_next_u32(&brain.rng()) % 12u) == 0u;
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
    // 4..10 blocks in a random direction; expiration forces the goal to
    // release even when a wall stops the mob short of the point.
    const double angle = (mcp_random_next_u32(&brain.rng()) /
                          (double)UINT32_MAX) * 2.0 * 3.14159265358979323846;
    const double dist = 4.0 + (mcp_random_next_u32(&brain.rng()) /
                               (double)UINT32_MAX) * 6.0;
    target_x_ = ctx.x + std::cos(angle) * dist;
    target_z_ = ctx.z + std::sin(angle) * dist;
    expires_tick_ = ctx.tick + 120 + (mcp_random_next_u32(&brain.rng()) % 120);
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
    // The species stroll pace: one profile table drives every kind, so
    // a cow trots while a chicken scurries and no goal hardcodes speed.
    brain.setMoveIntent(dx / len, dz / len, ctx.walkSpeed);
}

// ---- LookAtPlayerGoal ----

bool LookAtPlayerGoal::canStart(const MobContext& ctx, MobBrain&) {
    return ctx.playerNear && ctx.playerDist <= start_range_;
}

bool LookAtPlayerGoal::shouldContinue(const MobContext& ctx) const {
    return ctx.playerNear && ctx.playerDist <= stop_range_;
}

void LookAtPlayerGoal::tick(const MobContext& ctx, MobBrain& brain) {
    // Head yaw AND pitch: the head tracks the player's eye line, so a
    // player standing on a ledge or crouching by a pond gets a real
    // head tilt instead of a flat stare. Protocol-84 pitch is degrees
    // with positive = down, hence the negated atan2. The pivot is the
    // species eye height, so a cow raises its whole head to a player
    // on a hill while a rabbit barely tilts.
    const double dx = ctx.playerX - ctx.x;
    const double dz = ctx.playerZ - ctx.z;
    const double dy = (ctx.playerY + kPlayerEyeHeight) - (ctx.y + ctx.eyeHeight);
    const double flat = std::sqrt(dx * dx + dz * dz);
    const float pitch = flat > 1e-6
                            ? (float)(-std::atan2(dy, flat) * 180.0 /
                                      3.14159265358979323846)
                            : 0.0f;
    brain.setLookIntent(faceYaw(dx, dz), pitch);
}

// ---- IdleLookGoal ----

bool IdleLookGoal::canStart(const MobContext&, MobBrain& brain) {
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

// ---- PanicGoal ----

bool PanicGoal::canStart(const MobContext& ctx, MobBrain&) {
    // The reflex is record-driven: the damage pipeline stamped the hit
    // tick and the away-from-threat direction, and the manager's
    // context carries both. Only passive species carry this goal, so
    // hostiles and neutrals keep their own (combat) behaviors.
    return ctx.panicActive && ctx.panicSpeed > 0.0f;
}

bool PanicGoal::shouldContinue(const MobContext& ctx) const {
    return ctx.panicActive && ctx.panicSpeed > 0.0f;
}

void PanicGoal::tick(const MobContext& ctx, MobBrain& brain) {
    // Re-aim the flee every few ticks: away from the threat, deflected
    // by a per-mob random lean so a herd scatters instead of running in
    // lockstep, and small enough that the bolt reads as one flight —
    // not as an erratic zig-zag. Between re-aims the aim is constant,
    // so the stride and the leg animation stay smooth.
    if (reaim_tick_ == 0 || ctx.tick >= reaim_tick_) {
        const double lean =
            (((int64_t)mcp_random_next_u32(&brain.rng()) % 81u) - 40u) *
            (3.14159265358979323846 / 180.0);
        const double c = std::cos(lean), s = std::sin(lean);
        aim_x_ = ctx.panicDirX * c - ctx.panicDirZ * s;
        aim_z_ = ctx.panicDirX * s + ctx.panicDirZ * c;
        reaim_tick_ = ctx.tick + 15 + (mcp_random_next_u32(&brain.rng()) % 10);
    }
    brain.setMoveIntent(aim_x_, aim_z_, ctx.panicSpeed);
}

} // namespace cursestone::entity::ai
