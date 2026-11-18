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
#include "entity/ai/MobAIManager.h"
#include "Server.h"
#include "Player.h"
#include "entity/EntityTypes.h"
#include "math/AxisAlignedBB.h"
#include "math/Vector3.h"
#include "physics/Physics.h"
#include "world/World.h"
#include <chrono>
#include <cmath>
#include <unordered_set>
namespace cursestone::entity::ai {
namespace {

// Budgets and cadences (docs/MOB_AI.md section 4). The budget mirrors
// the light engine's contract: under load mobs slow down, the server
// does not.
constexpr double kMobBudgetMs = 2.0;    ///< Wall-clock cap of one pass.
constexpr uint64_t kThinkInterval = 10; ///< Ticks between goal passes.
constexpr uint64_t kBrainGCInterval = 200; ///< Brains sweep cadence.
constexpr double kLookRange = 8.0;      ///< Player range the goals see.

// Movement integration constants, matching the era entity model the
// default Entity tick and the item integrator already use.
constexpr float kGravity = 0.08f;
constexpr float kDrag = 0.98f;
constexpr float kJumpVelocity = 0.42f;
constexpr float kMaxFallSpeed = -3.92f;
constexpr double kHalfWidth = 0.3;  ///< Mob half-width (0.6 box like players).
constexpr double kHeight = 1.8;     ///< Collision height.
constexpr int kJumpCooldownTicks = 10;
constexpr double kJumpProbe = 0.7;  ///< Horizontal probe distance for steps.

// Broadcast gating: a mob that moved less than this since its last
// packet is not re-sent (the same threshold the item integrator uses).
constexpr double kBroadcastEps = 0.0005;
constexpr float kYawEps = 2.0f;     ///< Degrees of yaw change worth a packet.

/// @brief Absolute angle difference in degrees, wrapped to [0, 180].
float yawDelta(float a, float b) {
    float d = a - b;
    while (d > 180.0f)
        d -= 360.0f;
    while (d < -180.0f)
        d += 360.0f;
    return std::fabs(d);
}

} // namespace

MobAIManager::MobAIManager(World* owner) : world_(owner) {
}

void MobAIManager::tick(uint64_t current_tick) {
    buildPlayerSnapshot();
    // No presence anywhere: every mob is gated off, nothing to do. The
    // brains keep their timers for when a player walks back in.
    if (players_.empty())
        return;

    // Snapshot of the records under the world's mob lock. All callers
    // that mutate the map run on the main thread, so the pointers are
    // stable for the rest of this pass.
    const std::vector<world::MobEntity*> mobs = world_->mobs();
    if (mobs.empty())
        return;

    if (current_tick % kBrainGCInterval == 0) {
        // Drop brains of mobs that were removed (plugins despawn, egg
        // cleanup). Mob ids never repeat, so a stale brain is garbage.
        std::unordered_set<int64_t> live;
        live.reserve(mobs.size());
        for (const world::MobEntity* m : mobs)
            live.insert(m->eid);
        for (auto it = brains_.begin(); it != brains_.end();) {
            if (live.count(it->first) == 0)
                it = brains_.erase(it);
            else
                ++it;
        }
    }

    const auto pass_start = std::chrono::steady_clock::now();
    const size_t n = mobs.size();
    for (size_t k = 0; k < n; ++k) {
        world::MobEntity* m = mobs[(cursor_ + k) % n];
        if (!m)
            continue;

        MobBrain& brain = [&]() -> MobBrain& {
            auto it = brains_.find(m->eid);
            if (it != brains_.end())
                return *it->second;
            // Lazy creation: a mob spawned mid-pass gets its brain the
            // first time the cursor reaches it. Broadcast cache starts
            // at the record so idle mobs send nothing until they move.
            it = brains_
                     .emplace(m->eid,
                              makeMobBrain((uint8_t)entity::mobTypeKind(m->type),
                                           (uint64_t)m->eid))
                     .first;
            it->second->broadcast().valid = false;
            return *it->second;
        }();

        if (!brain.dynamic()) {
            // Static kind (no behavior stage yet): exactly the pre-AI
            // behavior — no physics, no packets.
            continue;
        }

        const int32_t mcx = m->cx;
        const int32_t mcz = m->cz;
        if (!gateOpen(mcx, mcz))
            continue; // Frozen: no think, no physics, no packets.

        MobContext ctx = buildContext(*m, brain, current_tick);
        if ((current_tick + (uint64_t)m->eid % kThinkInterval) % kThinkInterval ==
            0) {
            thinkOne(brain, ctx);
        }
        actuateOne(*m, brain, ctx);

        const double elapsed = std::chrono::duration<double, std::milli>(
                                   std::chrono::steady_clock::now() - pass_start)
                                   .count();
        if (elapsed >= kMobBudgetMs) {
            // Budget spent: hand the remaining mobs to the next tick,
            // starting one past the mob we just processed.
            cursor_ = (cursor_ + k + 1) % n;
            return;
        }
    }
    cursor_ = 0; // Full pass completed.
}

void MobAIManager::buildPlayerSnapshot() {
    players_.clear();
    for (const auto& p : Server::getInstance().playersSnapshotRef()) {
        if (!p || p->isPendingRemoval() || !p->spawned())
            continue;
        if (p->getWorld() != world_)
            continue;
        PlayerAnchor a;
        a.x = p->getNextPosition().x;
        a.y = p->getNextPosition().y;
        a.z = p->getNextPosition().z;
        a.cx = p->chunkX();
        a.cz = p->chunkZ();
        players_.push_back(a);
    }
}

bool MobAIManager::gateOpen(int32_t mcx, int32_t mcz) const {
    const int32_t r = (int32_t)world_->config().view_distance;
    for (const PlayerAnchor& p : players_) {
        const int32_t dx = p.cx - mcx;
        const int32_t dz = p.cz - mcz;
        if (dx <= r && dx >= -r && dz <= r && dz >= -r)
            return true;
    }
    return false;
}

MobContext MobAIManager::buildContext(const world::MobEntity& m,
                                      const MobBrain& brain,
                                      uint64_t current_tick) {
    MobContext ctx;
    ctx.eid = m.eid;
    ctx.type = m.type;
    ctx.kind = (uint8_t)entity::mobTypeKind(m.type);
    ctx.x = m.x;
    ctx.y = m.y;
    ctx.z = m.z;
    ctx.yaw = m.yaw;
    ctx.onGround = brain.onGround();

    // Nearest spawned player within look range. Players are few, so the
    // linear scan is cheaper than any spatial index would be.
    double best2 = kLookRange * kLookRange;
    ctx.playerNear = false;
    for (const PlayerAnchor& p : players_) {
        const double dx = p.x - m.x;
        const double dz = p.z - m.z;
        const double d2 = dx * dx + dz * dz;
        if (d2 < best2) {
            best2 = d2;
            ctx.playerNear = true;
            ctx.playerDist = std::sqrt(d2);
            ctx.playerX = p.x;
            ctx.playerY = p.y;
            ctx.playerZ = p.z;
        }
    }
    ctx.tick = current_tick;
    return ctx;
}

void MobAIManager::thinkOne(MobBrain& brain, const MobContext& ctx) {
    brain.stepThink(ctx);
}

void MobAIManager::actuateOne(world::MobEntity& m, MobBrain& brain,
                              const MobContext& ctx) {
    // Unloaded terrain would read as air and drop the mob through the
    // world; the gate plus this guard keeps physics on resident chunks
    // only (the same rule world.setBlock follows).
    if (!world_->isChunkLoaded(m.cx, m.cz))
        return;
    if (m.y < 0.0)
        return; // Void: nothing below to integrate against.

    const MobIntent& intent = brain.intent();

    // Facing: an explicit look intent wins, a movement intent faces the
    // direction of travel, otherwise the mob keeps its last rotation.
    float targetYaw = m.yaw;
    if (intent.look)
        targetYaw = intent.yaw;
    else if (intent.move)
        targetYaw = faceYaw(intent.dirX, intent.dirZ);

    const bool idle = !intent.move && brain.onGround() && !brain.wantsJump();
    if (idle) {
        // Standing mob: physics is a no-op; only a rotation change is
        // worth a packet.
        MobBrain::BroadcastCache& bc = brain.broadcast();
        if (!bc.valid) {
            bc.valid = true;
            bc.x = m.x;
            bc.y = m.y;
            bc.z = m.z;
            bc.yaw = m.yaw;
            return;
        }
        if (yawDelta(targetYaw, bc.yaw) >= kYawEps) {
            if (world_->moveMob(m.eid, m.x, m.y, m.z, targetYaw, 0.0f)) {
                bc.x = m.x;
                bc.y = m.y;
                bc.z = m.z;
                bc.yaw = targetYaw;
            } else {
                bc.valid = false;
            }
        }
        return;
    }

    // ---- Physics integration (the item/falling-block sweep shape) ----
    float& vy = brain.motionY();
    if (brain.onGround()) {
        vy = brain.wantsJump() ? kJumpVelocity : 0.0f;
        brain.wantsJump() = false;
    } else {
        vy -= kGravity;
        vy *= kDrag;
        if (vy < kMaxFallSpeed)
            vy = kMaxFallSpeed;
    }

    double dx = 0.0, dz = 0.0;
    if (intent.move) {
        dx = intent.dirX * (double)intent.speed;
        dz = intent.dirZ * (double)intent.speed;
    }

    const mcp_aabb_t bb =
        mcp_aabb_make(m.x - kHalfWidth, m.y, m.z - kHalfWidth, m.x + kHalfWidth,
                      m.y + kHeight, m.z + kHalfWidth);
    const mcp_aabb_t lookup = mcp_aabb_clone_move(bb, dx, vy, dz);
    world_->collisionBoxes(lookup, sweep_boxes_);

    physics::MotionResult res = physics::sweep(bb, mcp_v3(dx, vy, dz), sweep_boxes_);
    const double nx = m.x + res.motion.x;
    const double ny = m.y + res.motion.y;
    const double nz = m.z + res.motion.z;
    brain.onGround() = res.on_ground;

    // Step-up: a horizontal clip with headroom above the obstacle is a
    // one-block step, worth a jump. A clipped column at head height is
    // a wall, and jumping would only grind against it.
    if ((res.clipped_x || res.clipped_z) && res.on_ground && intent.move &&
        brain.jumpCooldown() <= 0) {
        const int bx = (int)std::floor(m.x + intent.dirX * kJumpProbe);
        const int bz = (int)std::floor(m.z + intent.dirZ * kJumpProbe);
        const int feetY = (int)std::floor(m.y);
        const bool chestSolid =
            World::entityCollidable(world_->getBlockId(bx, feetY + 1, bz));
        const bool headSolid =
            World::entityCollidable(world_->getBlockId(bx, feetY + 2, bz));
        if (!chestSolid && !headSolid) {
            brain.wantsJump() = true;
            brain.jumpCooldown() = kJumpCooldownTicks;
        }
    }
    if (brain.jumpCooldown() > 0)
        --brain.jumpCooldown();

    // Broadcast only what changed since the last packet.
    MobBrain::BroadcastCache& bc = brain.broadcast();
    const bool moved = !bc.valid ||
                       std::fabs(nx - bc.x) > kBroadcastEps ||
                       std::fabs(ny - bc.y) > kBroadcastEps ||
                       std::fabs(nz - bc.z) > kBroadcastEps;
    const bool turned = yawDelta(targetYaw, bc.yaw) >= kYawEps;
    if (moved || turned) {
        if (world_->moveMob(m.eid, nx, ny, nz, targetYaw, 0.0f)) {
            bc.valid = true;
            bc.x = nx;
            bc.y = ny;
            bc.z = nz;
            bc.yaw = targetYaw;
        } else {
            bc.valid = false; // Out-of-height move refused; re-sync next tick.
        }
    }
}

} // namespace cursestone::entity::ai
