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
#include "block/BlockIds.h"
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

// Knockback: the consumed shove decays with the era ground friction (the
// same "motion shrinks every tick" shape the entity model uses; 0.6 keeps
// the ~1 block shove the player-vs-player melee produces).
constexpr float kKnockbackFriction = 0.6f;

// Broadcast gating: a mob that moved less than this since its last
// packet is not re-sent (the same threshold the item integrator uses).
constexpr double kBroadcastEps = 0.0005;
constexpr float kYawEps = 2.0f;     ///< Degrees of yaw change worth a packet.
constexpr float kPitchEps = 2.0f;   ///< Degrees of pitch change worth a
                                    ///< packet (the wire encodes pitch as
                                    ///< a byte, ~1.4 degree resolution).

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
        if (m->death_tick != 0 || m->health <= 0.0f)
            continue; // Dying: the death window owns the body (no AI, no physics).

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

bool MobAIManager::isWaterCell(double x, double y, double z) const {
    const uint8_t id = world_->getBlockId((int32_t)std::floor(x),
                                          (int32_t)std::floor(y),
                                          (int32_t)std::floor(z));
    return id == block::FLOWING_WATER || id == block::WATER;
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

    // Water snapshot for the swim model: the feet and chest cells make
    // the body buoyant, the head cell decides the surfacing paddle.
    // Three block reads per build, same resident-chunk contract the
    // actuation's block probes already follow.
    ctx.inWater = isWaterCell(m.x, m.y, m.z) || isWaterCell(m.x, m.y + 0.5, m.z);
    ctx.headInWater = isWaterCell(m.x, m.y + 1.5, m.z);

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

    // Consume the record's pending knockback: a fresh shove becomes brain
    // carry-over (plus the grounded hop), a stale field just expires — the
    // record fields are cross-thread (damage can arrive from the plugin
    // APIs), so the clear runs under the world's mob lock.
    if (m.kb_tick != 0) {
        const bool fresh = ctx.tick >= m.kb_tick && ctx.tick - m.kb_tick <= 5;
        if (fresh) {
            brain.motionX() = m.kbx;
            brain.motionZ() = m.kbz;
            if (brain.onGround())
                brain.wantsJump() = true; // The vertical pop of the hit.
        }
        {
            std::lock_guard<std::mutex> lk(world_->mobMutex());
            m.kbx = 0.0f;
            m.kbz = 0.0f;
            m.kb_tick = 0;
        }
    }

    const MobIntent& intent = brain.intent();

    // Facing: the body follows the movement direction while the mob
    // travels (the 0.15.10 client renders the body from the move
    // packet's yaw — body and head in one yaw read as moonwalking),
    // the head tracks the look target when a goal is looking, and a
    // standing mob squares its body up to its head past the follow
    // angle. resolveFacing owns the whole rule set.
    const ai::FacingStep facing =
        ai::resolveFacing(m.yaw, m.head_yaw, m.pitch, intent,
                          /*standing=*/brain.onGround());

    const bool idle = !intent.move && !brain.hasKnockback() && brain.onGround() &&
                      !brain.wantsJump();
    if (idle) {
        // Standing mob: physics is a no-op; only a rotation change is
        // worth a packet.
        MobBrain::BroadcastCache& bc = brain.broadcast();
        if (!bc.valid) {
            bc.valid = true;
            bc.x = m.x;
            bc.y = m.y;
            bc.z = m.z;
            bc.yaw = facing.bodyYaw;
            bc.head_yaw = facing.headYaw;
            bc.pitch = facing.pitch;
            return;
        }
        const bool turned = yawDelta(facing.bodyYaw, bc.yaw) >= kYawEps ||
                            yawDelta(facing.headYaw, bc.head_yaw) >= kYawEps ||
                            std::fabs(facing.pitch - bc.pitch) >= kPitchEps;
        if (turned) {
            if (world_->moveMob(m.eid, m.x, m.y, m.z, facing.bodyYaw,
                                facing.pitch, facing.headYaw)) {
                bc.x = m.x;
                bc.y = m.y;
                bc.z = m.z;
                bc.yaw = facing.bodyYaw;
                bc.head_yaw = facing.headYaw;
                bc.pitch = facing.pitch;
            } else {
                bc.valid = false;
            }
        }
        return;
    }

    // ---- Physics integration (the item/falling-block sweep shape) ----
    float& vy = brain.motionY();
    if (ctx.inWater) {
        // The swim model replaces the fall: buoyancy, paddle and drag
        // integrate to a surface bob; the shore hop spends the pending
        // jump as an upward shove against the bank.
        const ai::WaterVerticalStep water = ai::waterVerticalStep(
            vy, ctx.headInWater, intent.move, brain.wantsJump());
        vy = water.vy;
        if (water.jumpConsumed) {
            brain.wantsJump() = false;
            brain.jumpCooldown() = kJumpCooldownTicks;
        }
    } else if (brain.onGround()) {
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
        // Water halves the stride: swimming is slower than walking, in
        // the era entity model and in the reference.
        const double stride = ctx.inWater ? (double)intent.speed * 0.5
                                          : (double)intent.speed;
        dx = intent.dirX * stride;
        dz = intent.dirZ * stride;
    }
    // A consumed shove rides on top of the goal's own step.
    dx += (double)brain.motionX();
    dz += (double)brain.motionZ();

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
    brain.decayKnockback(kKnockbackFriction);

    // Step-up: a horizontal clip with headroom above the obstacle is a
    // one-block step, worth a jump. A clipped column at head height is
    // a wall, and jumping would only grind against it. In water the
    // same clip is a shore: the pending jump becomes the swim hop that
    // climbs the bank (no onGround requirement — a swimming mob has
    // none).
    if ((res.clipped_x || res.clipped_z) && intent.move &&
        brain.jumpCooldown() <= 0) {
        if (ctx.inWater) {
            brain.wantsJump() = true;
            brain.jumpCooldown() = kJumpCooldownTicks;
        } else if (res.on_ground) {
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
    }
    if (brain.jumpCooldown() > 0)
        --brain.jumpCooldown();

    // Broadcast only what changed since the last packet.
    MobBrain::BroadcastCache& bc = brain.broadcast();
    const bool moved = !bc.valid ||
                       std::fabs(nx - bc.x) > kBroadcastEps ||
                       std::fabs(ny - bc.y) > kBroadcastEps ||
                       std::fabs(nz - bc.z) > kBroadcastEps;
    const bool turned = yawDelta(facing.bodyYaw, bc.yaw) >= kYawEps ||
                        yawDelta(facing.headYaw, bc.head_yaw) >= kYawEps ||
                        std::fabs(facing.pitch - bc.pitch) >= kPitchEps;
    if (moved || turned) {
        if (world_->moveMob(m.eid, nx, ny, nz, facing.bodyYaw,
                            facing.pitch, facing.headYaw)) {
            bc.valid = true;
            bc.x = nx;
            bc.y = ny;
            bc.z = nz;
            bc.yaw = facing.bodyYaw;
            bc.head_yaw = facing.headYaw;
            bc.pitch = facing.pitch;
        } else {
            bc.valid = false; // Out-of-height move refused; re-sync next tick.
        }
    }
}

} // namespace cursestone::entity::ai
