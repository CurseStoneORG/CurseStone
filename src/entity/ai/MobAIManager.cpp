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
#include "entity/ai/MobAIManager.h"
#include "Server.h"
#include "Player.h"
#include "block/BlockIds.h"
#include "entity/EntityTypes.h"
#include "entity/ai/AiMath.h"
#include "math/AxisAlignedBB.h"
#include "math/Vector3.h"
#include "physics/Physics.h"
#include "utils/MainLogger.hpp"
#include "world/Explosion.h"
#include "world/World.h"
#include "network/protocol/SetEntityDataPacket.h"
#include "world/particle/WaterSplashParticle.h"
#include "world/particle/WaterWakeParticle.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <unordered_set>
namespace cursestone::entity::ai {
namespace {

// Budgets and cadences. The budget mirrors the light engine's contract:
// under load mobs slow down, the server does not. The base budget grows
// with the population (bounded well under the tick budget) so a herd
// near the players keeps its smooth per-tick simulation instead of
// starving the moment a few thousand mobs are loaded.
constexpr double kMobBudgetMs = 2.0;      ///< Wall-clock cap, small worlds.
constexpr double kMobBudgetMaxMs = 8.0;   ///< Absolute wall-clock cap.
constexpr double kMobBudgetPerMob = 1.0 / 2000.0; ///< Extra ms per record.
constexpr uint64_t kBrainGCInterval = 200; ///< Brains drift sweep cadence.

// Movement integration constants, matching the era entity model the
// default Entity tick and the item integrator already use.
constexpr float kGravity = 0.08f;
constexpr float kDrag = 0.98f;
constexpr float kJumpVelocity = 0.42f;
constexpr float kMaxFallSpeed = -3.92f;
constexpr int kJumpCooldownTicks = 10;
constexpr double kJumpProbe = 0.7; ///< Horizontal probe distance for steps.

// Water physics (the era travel model): gravity drops to a quarter, the
// vertical motion shrinks to 80% per tick, and the FloatGoal paddle
// (the jump channel) adds the era 0.04 per tick.
constexpr float kWaterGravity = 0.02f;
constexpr float kWaterDrag = 0.8f;
constexpr float kSwimPaddle = 0.04f;
/// @brief The shore-exit hop: a horizontal clip against a bank while
///        swimming spends this as an upward jump.
constexpr float kWaterJumpOut = 0.30f;

// Knockback: the consumed shove decays with the era ground friction (the
// same "motion shrinks every tick" shape the entity model uses; 0.6 keeps
// the ~1 block shove the player-vs-player melee produces).
constexpr float kKnockbackFriction = 0.6f;

// Broadcast gating: a mob that moved less than this since its last
// packet is not re-sent (the same threshold the item integrator uses).
constexpr double kBroadcastEps = 0.0005;
constexpr float kYawEps = 2.0f;   ///< Degrees of yaw change worth a packet.
constexpr float kPitchEps = 2.0f; ///< Degrees of pitch change worth a
                                  ///< packet (the wire encodes pitch as
                                  ///< a byte, ~1.4 degree resolution).

// Water effects. The wake rides behind a mob that is actually moving
// through water (never while it floats still), rate limited to one
// particle burst every few ticks; the entry splash fires once on the
// air->water transition when the mob fell in with real speed.
constexpr int kWakeIntervalTicks = 4;
constexpr float kEntrySplashMinFall = 0.15f;

// Water surface rendering: the era client draws the top of a water
// column 2/16 of a block below the block top (the 14/16 water height).
constexpr float kWaterSurfaceOffset = 0.875f;

/// @brief Absolute angle difference in degrees, wrapped to [0, 180].
float yawDelta(float a, float b) {
    float d = a - b;
    while (d > 180.0f)
        d -= 360.0f;
    while (d < -180.0f)
        d += 360.0f;
    return std::fabs(d);
}

bool waterId(uint8_t id) {
    return id == block::FLOWING_WATER || id == block::WATER;
}

/// @brief Deterministic per-mob-tick jitter in [-0.5, 0.5): a cheap mix
///        of the entity id and the tick, so particle placement scatters
///        without spending the brain RNG the goals own.
double jitter(int64_t eid, uint64_t tick, int salt) {
    uint64_t h = (uint64_t)eid * 0x9E3779B97F4A7C15ull;
    h ^= tick + (uint64_t)salt * 0x517CC1B727220A95ull;
    h ^= h >> 29;
    return ((double)(h % 1024u) / 1024.0) - 0.5;
}

} // namespace

MobAIManager::MobAIManager(World* owner) : world_(owner) {
}

void MobAIManager::forget(int64_t eid) {
    brains_.erase(eid);
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

void MobAIManager::rebuildActiveChunks(uint64_t now) {
    // Layout signature: player count plus the low bits of every chunk
    // coordinate. When it holds and the refresh timer has not elapsed,
    // the cached key list is still exact.
    uint64_t sig = players_.size() * 0x9E3779B97F4A7C15ull;
    for (const PlayerAnchor& p : players_)
        sig = sig * 31 + (uint64_t)(uint32_t)p.cx * 31 + (uint64_t)(uint32_t)p.cz;
    if (sig == players_chunk_sig_ && now - active_refresh_tick_ < 40)
        return;
    players_chunk_sig_ = sig;
    active_refresh_tick_ = now;

    active_keys_.clear();
    const int32_t r = (int32_t)world_->config().view_distance;
    // The visited set is the union of every player's view square. With
    // a handful of players the square walk is cheap; overlapping
    // squares dedupe through the set.
    std::unordered_set<uint64_t> seen;
    seen.reserve(players_.size() * (size_t)(2 * r + 1) * (size_t)(2 * r + 1));
    for (const PlayerAnchor& p : players_) {
        for (int32_t dx = -r; dx <= r; ++dx) {
            for (int32_t dz = -r; dz <= r; ++dz) {
                const uint64_t key = World::chunkKey(p.cx + dx, p.cz + dz);
                if (seen.insert(key).second)
                    active_keys_.push_back(key);
            }
        }
    }
    // Distance-ordered walk: the pass starts at the chunks the players
    // actually look at and walks outward, so a budget that runs out
    // under heavy populations degrades the FARTHEST mobs first. The
    // visible world keeps its smooth per-tick simulation; the outer
    // rings fall back to reduced cadence and, past the budget, to a
    // held pose — exactly the vanilla LOD direction.
    std::sort(active_keys_.begin(), active_keys_.end(),
              [this](uint64_t a, uint64_t b) {
                  const int32_t ax = (int32_t)(a >> 32), az = (int32_t)(a & 0xFFFFFFFFu);
                  const int32_t bx = (int32_t)(b >> 32), bz = (int32_t)(b & 0xFFFFFFFFu);
                  return nearestPlayerChunkDist(ax, az) <
                         nearestPlayerChunkDist(bx, bz);
              });
}

int32_t MobAIManager::nearestPlayerChunkDist(int32_t mcx, int32_t mcz) const {
    int32_t best = INT32_MAX;
    for (const PlayerAnchor& p : players_) {
        const int32_t dx = p.cx - mcx;
        const int32_t dz = p.cz - mcz;
        const int32_t d = std::max(std::abs(dx), std::abs(dz));
        if (d < best)
            best = d;
    }
    return best;
}

bool MobAIManager::isWaterCell(double x, double y, double z) const {
    return waterId(world_->getBlockId((int32_t)std::floor(x),
                                      (int32_t)std::floor(y),
                                      (int32_t)std::floor(z)));
}

float MobAIManager::waterSurfaceY(double x, double y, double z) const {
    // Walk up from the feet cell while the column is water; the surface
    // sits 2/16 under the top of the last water block. The scan is
    // bounded: floating bodies never stand under more than a couple of
    // stacked water blocks.
    int32_t cell = (int32_t)std::floor(y);
    for (int i = 0; i < 8; ++i) {
        if (!waterId(world_->getBlockId((int32_t)std::floor(x), cell + 1,
                                        (int32_t)std::floor(z))))
            break;
        ++cell;
    }
    return (float)cell + kWaterSurfaceOffset;
}

void MobAIManager::waterEffects(world::MobEntity& m, MobBrain& brain,
                                const PlayerContext& ctx, bool moved,
                                double move_dx, double move_dz) {
    if (!ctx.inWater) {
        brain.inWater() = false;
        return;
    }
    const bool was_in = brain.inWater();
    brain.inWater() = true;
    if (!was_in) {
        // Air -> water transition. A real fall-in (the knock-on of a
        // steep entry) bursts the era splash particles at the surface —
        // the same ids the player's own water entry uses; a quiet walk
        // into a ford stays quiet and only starts leaving a wake once
        // it swims.
        if (brain.motionY() < -kEntrySplashMinFall) {
            for (int i = 0; i < 3; ++i) {
                const double jx = jitter(m.eid, ctx.tick, i) * 0.6;
                const double jz = jitter(m.eid, ctx.tick, i + 16) * 0.6;
                world_->addParticle(
                    world::particle::WaterSplashParticle(
                        world::Position{(float)(m.x + jx),
                                        (float)ctx.waterSurface,
                                        (float)(m.z + jz)}));
            }
            world_->addParticle(world::particle::WaterWakeParticle(
                world::Position{(float)m.x, (float)ctx.waterSurface,
                                (float)m.z}));
            brain.splashCooldown() = kWakeIntervalTicks;
        }
        return;
    }
    // Swim wake: one burst every few ticks, only while the body is
    // actually moving — a floating mob leaves the surface glass-flat.
    if (!moved || brain.splashCooldown()-- > 0)
        return;
    brain.splashCooldown() = kWakeIntervalTicks;
    const double jx = jitter(m.eid, ctx.tick, 7) * 0.3;
    const double jz = jitter(m.eid, ctx.tick, 9) * 0.3;
    world_->addParticle(world::particle::WaterWakeParticle(
        world::Position{(float)(m.x - move_dx * 0.5 + jx),
                        (float)(ctx.waterSurface - 0.05f),
                        (float)(m.z - move_dz * 0.5 + jz)}));
}

void MobAIManager::tick(uint64_t current_tick) {
    buildPlayerSnapshot();
    // No presence anywhere: every mob is gated off, nothing to do. The
    // brains keep their timers for when a player walks back in.
    if (players_.empty())
        return;

    rebuildActiveChunks(current_tick);
    if (active_keys_.empty())
        return;

    if (current_tick % kBrainGCInterval == 0 && brains_.size() > world_->mobCount() + 64) {
        // Drift sweep: forget() keeps the brain map in step with the
        // record map on every removal path; a drift above the slack
        // means a path missed the hook, so pay the one-off rebuild.
        std::unordered_set<int64_t> live;
        live.reserve(world_->mobCount());
        for (const uint64_t key : active_keys_) {
            for (world::MobEntity* m : world_->chunkMobs(
                     (int32_t)(key >> 32), (int32_t)(key & 0xFFFFFFFFu))) {
                if (m)
                    live.insert(m->eid);
            }
        }
        for (auto it = brains_.begin(); it != brains_.end();) {
            if (live.count(it->first) == 0)
                it = brains_.erase(it);
            else
                ++it;
        }
    }

    const auto pass_start = std::chrono::steady_clock::now();
    // The pass budget: the base cap plus a bounded per-record growth, so
    // the visible ring stays smooth as populations scale. 8 ms tops —
    // a sixth of the tick budget, and the distance order means a cut
    // lands on the outermost chunks first.
    const double budget_ms =
        std::min(kMobBudgetMaxMs,
                 kMobBudgetMs + kMobBudgetPerMob * (double)world_->mobCount());
    // One diagnostics line per window: the real work counters of the
    // pass (population, live brains, actuations, budget cuts).
    const auto log_stats = [this, &pass_start](uint64_t now) {
        if (now % kBrainGCInterval != 0)
            return;
        char line[256];
        std::snprintf(line, sizeof(line),
                      "[MOB AI] records=%zu brains=%zu actuated=%llu moves=%llu "
                      "mobmoved=%llu mobrefused=%llu budget_cuts=%llu chunks=%zu",
                      world_->mobCount(), brains_.size(),
                      (unsigned long long)stat_actuated_,
                      (unsigned long long)stat_moves_,
                      (unsigned long long)stat_mob_moved_,
                      (unsigned long long)stat_mob_refused_,
                      (unsigned long long)stat_budget_stops_,
                      active_keys_.size());
        utils::MainLogger::get().info(line);
        stat_actuated_ = 0;
        stat_moves_ = 0;
        stat_mob_moved_ = 0;
        stat_mob_refused_ = 0;
        stat_budget_stops_ = 0;
        stat_window_tick_ = now;
    };
    const size_t chunk_count = active_keys_.size();
    // Budgeted distance-ordered pass: chunks nearest the players are
    // visited first every tick, the walk runs outward until the
    // wall-clock budget is spent. Mob records are snapshot per chunk
    // (small vectors under the mob lock), so the pass never
    // materializes the whole population and never holds the lock while
    // physics or packets run.
    for (size_t slot = 0; slot < chunk_count; ++slot) {
        const uint64_t key = active_keys_[slot];
        const std::vector<world::MobEntity*> mobs = world_->chunkMobs(
            (int32_t)(key >> 32), (int32_t)(key & 0xFFFFFFFFu));
        for (world::MobEntity* m : mobs) {
            if (!m)
                continue;
            if (m->death_tick != 0 || m->health <= 0.0f)
                continue; // Dying: the death window owns the body (no AI, no physics).

            MobBrain& brain = [&]() -> MobBrain& {
                auto it = brains_.find(m->eid);
                if (it != brains_.end())
                    return *it->second;
                // Lazy creation: a mob spawned mid-pass gets its brain
                // the first time the cursor reaches it. Broadcast cache
                // starts at the record so idle mobs send nothing until
                // they move.
                it = brains_
                         .emplace(m->eid,
                                  makeMobBrain(m->type, (uint64_t)m->eid))
                         .first;
                it->second->broadcast().valid = false;
                return *it->second;
            }();

            if (!brain.dynamic()) {
                // Static kind (flight, hops, aquatic): exactly the
                // pre-AI behavior — no physics, no packets.
                continue;
            }

            // Tier: full pipeline near players every tick, goal pass at
            // reduced cadence at the edge of the view distance. The
            // panic window always runs full: the flight must read
            // smoothly. The actuation runs every tick for both tiers:
            // the vanilla fall integration and the move stream must
            // advance per tick, only the A*/sensing work throttles.
            const uint64_t now = Server::getInstance().getCurrentTick();
            const bool panicActive =
                m->panic_tick != 0 && now >= m->panic_tick &&
                now - m->panic_tick <= 100;
            const int32_t player_dist = nearestPlayerChunkDist(m->cx, m->cz);
            const bool full_tier =
                player_dist <= kFullTierRadius || panicActive;
            const bool goal_tick = full_tier ||
                                   (current_tick +
                                    (uint64_t)m->eid % kReducedInterval) %
                                           kReducedInterval ==
                                       0;

            // The era pipeline: one newServerAiStep (targets, goals,
            // navigation, mob step, controls) then one actuation.
            PlayerContext ctx;
            ctx.tick = current_tick;
            ctx.inWater = isWaterCell(m->x, m->y, m->z) ||
                          isWaterCell(m->x, m->y + 0.5, m->z);
            ctx.headInWater = isWaterCell(m->x, m->y + 1.5, m->z);
            if (ctx.inWater)
                ctx.waterSurface = waterSurfaceY(m->x, m->y, m->z);
            ctx.panicActive = panicActive;
            if (goal_tick)
                brain.newServerAiStep(world_, *m, current_tick);

            // The creeper fuse finished mid-step: detonate and remove.
            if (goal_tick && brain.wantsExplode()) {
                const int64_t eid = m->eid;
                const double ex = m->x, ey = m->y + 0.5, ez = m->z;
                const int32_t cx = m->cx, cz = m->cz;
                world::Explosion explosion(world::Position{ex, ey, ez}, 3.0f);
                explosion.explode();
                world_->removeMob(eid);
                (void)cx;
                (void)cz;
                ++stat_actuated_;
                const double elapsed =
                    std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - pass_start)
                        .count();
                if (elapsed >= budget_ms) {
                    ++stat_budget_stops_;
                    log_stats(current_tick);
                    return;
                }
                continue;
            }

            actuateOne(*m, brain, ctx, current_tick, goal_tick);
            ++stat_actuated_;

            const double elapsed = std::chrono::duration<double, std::milli>(
                                       std::chrono::steady_clock::now() - pass_start)
                                       .count();
            if (elapsed >= budget_ms) {
                // Budget spent: the outer chunks wait for the next
                // tick (the distance order already put the visible
                // world first).
                ++stat_budget_stops_;
                log_stats(current_tick);
                return;
            }
        }
    }

    log_stats(current_tick);
}

bool MobAIManager::actuateOne(world::MobEntity& m, MobBrain& brain,
                              const PlayerContext& ctx, uint64_t,
                              bool goal_ran) {
    // Resident terrain only: a mob whose chunk a viewer already left
    // must not sweep against air and fall through the world (the same
    // rule world.setBlock follows). The record's (cx, cz) are CHUNK
    // coordinates — the residency probe takes them directly.
    if (world_->chunks().getChunkIfLoaded(m.cx, m.cz) == nullptr)
        return false;
    if (m.y < 0.0)
        return false; // Void: nothing below to integrate against.

    // Consume the record's pending knockback: a fresh shove becomes brain
    // carry-over (plus the grounded hop), a stale field just expires — the
    // record fields are cross-thread (damage can arrive from the plugin
    // APIs), so the clear runs under the world's mob lock.
    if (m.kb_tick != 0) {
        const uint64_t now = Server::getInstance().getCurrentTick();
        const bool fresh = now >= m.kb_tick && now - m.kb_tick <= 5;
        if (fresh) {
            brain.motionX() = m.kbx;
            brain.motionZ() = m.kbz;
            if (brain.host().onGround())
                brain.wantsJump() = true; // The vertical pop of the hit.
        }
        {
            std::lock_guard<std::mutex> lk(world_->mobMutex());
            m.kbx = 0.0f;
            m.kbz = 0.0f;
            m.kb_tick = 0;
        }
    }

    const Mob& host = brain.host();

    // ---- Movement channel: on a goal tick the era MoveControl armed
    // the speed toward its wanted position and the direction rides the
    // movement yaw it steered. Between reduced-tier goal passes the
    // last armed stride persists through the brain's latch so the
    // physics still integrate every tick; the latch expires after
    // kReducedInterval ticks without an armed pass, the era hard stop.
    double dx = 0.0, dz = 0.0;
    float stride = 0.0f;
    float move_yaw = 0.0f;
    bool moving = false;
    if (goal_ran) {
        if (host.movingTick()) {
            moving = true;
            move_yaw = host.yaw();
            stride = host.speed();
            MobBrain::MoveLatch& latch = brain.moveLatch();
            latch.yaw = move_yaw;
            latch.stride = stride;
            latch.ttl = (uint32_t)kReducedInterval;
        } else {
            brain.moveLatch().ttl = 0;
        }
    } else if (brain.moveLatch().ttl > 0) {
        --brain.moveLatch().ttl;
        moving = true;
        move_yaw = brain.moveLatch().yaw;
        stride = brain.moveLatch().stride;
    }
    if (moving) {
        double dir_x = 0.0, dir_z = 0.0;
        math::yawDirection(move_yaw, dir_x, dir_z);
        // Water halves the stride: swimming is slower than walking, in
        // the era entity model and in the reference.
        const double effective =
            ctx.inWater ? (double)stride * 0.5 : (double)stride;
        dx = dir_x * effective;
        dz = dir_z * effective;
        stat_moves_ += 1;
    }
    // A consumed shove rides on top of the goal's own step.
    dx += (double)brain.motionX();
    dz += (double)brain.motionZ();

    // ---- Vertical: the era aiStep shape — a ground jump spends 0.42,
    // water integrates the reduced gravity with the paddle from the
    // jump channel.
    float vy = brain.motionY();
    const bool swimming = ctx.inWater && !brain.host().onGround();
    if (swimming) {
        vy -= kWaterGravity;
        vy *= kWaterDrag;
        if (host.isJumping())
            vy += kSwimPaddle;
        if (vy < -0.15f)
            vy = -0.15f; // A swimming body never sinks away.
        if (vy > 0.12f)
            vy = 0.12f;
        brain.wantsJump() = false;
    } else if (brain.host().onGround()) {
        vy = brain.wantsJump() ? kJumpVelocity : 0.0f;
        brain.wantsJump() = false;
    } else {
        vy -= kGravity;
        vy *= kDrag;
        if (vy < kMaxFallSpeed)
            vy = kMaxFallSpeed;
    }
    brain.motionY() = vy;

    const entity::MobProfile profile = entity::mobProfile(m.type);
    const double half_w = profile.width * 0.5;
    const mcp_aabb_t bb = mcp_aabb_make(m.x - half_w, m.y, m.z - half_w,
                                        m.x + half_w, m.y + profile.height,
                                        m.z + half_w);
    const mcp_aabb_t lookup = mcp_aabb_clone_move(bb, dx, vy, dz);
    world_->collisionBoxes(lookup, sweep_boxes_);

    physics::MotionResult res = physics::sweep(bb, mcp_v3(dx, vy, dz), sweep_boxes_);
    const double nx = m.x + res.motion.x;
    const double ny = m.y + res.motion.y;
    const double nz = m.z + res.motion.z;
    brain.host().setOnGround(res.on_ground);
    brain.decayKnockback(kKnockbackFriction);
    brain.host().setCollidedHorizontally(res.clipped_x || res.clipped_z);
    brain.host().setPrevPos(nx, nz);

    // Step-up: a horizontal clip against a one-block step (headroom
    // above it) is worth a jump; a clipped column at chest or head
    // height is a wall. In water the same clip is a shore: the pending
    // jump becomes the swim hop that climbs the bank (the era paddling
    // hop; no onGround requirement — a swimming body has none).
    if ((res.clipped_x || res.clipped_z) && moving &&
        brain.jumpCooldown() <= 0) {
        if (swimming) {
            brain.motionY() = kWaterJumpOut;
            brain.jumpCooldown() = kJumpCooldownTicks;
        } else if (res.on_ground) {
            double dir_x = 0.0, dir_z = 0.0;
            math::yawDirection(move_yaw, dir_x, dir_z);
            const int bx = (int)std::floor(m.x + dir_x * kJumpProbe);
            const int bz = (int)std::floor(m.z + dir_z * kJumpProbe);
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

    // Pose keys: the 0.15.10 client models read per-mob-class entity data
    // (the skeleton bow-draw flag and the creeper swell triple; the binary
    // mapping lives in docs/MOB_AI.md). The brain stamps the authoritative
    // values during the AI pass; only actual changes hit the wire — one
    // SetEntityDataPacket per changed tick, the era dirty-sync shape.
    MobBrain::PoseSync& pose = brain.poseSync();
    if (pose.charging != pose.chargingSent) {
        pose.chargingSent = pose.charging;
        using Meta = network::protocol::SetEntityDataPacket;
        world_->setMobDataBytes(
            m.eid, m.cx, m.cz,
            {{Meta::PROP_MOB_CHARGING, pose.charging ? 1 : 0}});
    }
    if (m.type == mobs::Creeper::kId &&
        (pose.swellDir != host.getSwellDir() ||
         pose.swell != host.swellTime ||
         pose.swellOld != host.prevSwellTime)) {
        pose.swellDir = host.getSwellDir();
        pose.swell = host.swellTime;
        pose.swellOld = host.prevSwellTime;
        using Meta = network::protocol::SetEntityDataPacket;
        world_->setMobDataBytes(
            m.eid, m.cx, m.cz,
            {{Meta::PROP_SWELL_DIR, (int8_t)pose.swellDir},
             {Meta::PROP_SWELL, (int8_t)pose.swell},
             {Meta::PROP_SWELL_OLD, (int8_t)pose.swellOld}});
    }

    // Broadcast only what changed since the last packet. Body yaw rides
    // the record's yaw channel (BodyControl reconciled it), head yaw and
    // pitch ride their own channels.
    MobBrain::BroadcastCache& bc = brain.broadcast();
    const bool movedFlag = !bc.valid ||
                           std::fabs(nx - bc.x) > kBroadcastEps ||
                           std::fabs(ny - bc.y) > kBroadcastEps ||
                           std::fabs(nz - bc.z) > kBroadcastEps;
    const bool turned = yawDelta(m.yaw, bc.yaw) >= kYawEps ||
                        yawDelta(m.head_yaw, bc.head_yaw) >= kYawEps ||
                        std::fabs(m.pitch - bc.pitch) >= kPitchEps;
    if (movedFlag || turned) {
        if (world_->moveMob(m.eid, nx, ny, nz, m.yaw, m.pitch, m.head_yaw)) {
            ++stat_mob_moved_;
            bc.valid = true;
            bc.x = nx;
            bc.y = ny;
            bc.z = nz;
            bc.yaw = m.yaw;
            bc.head_yaw = m.head_yaw;
            bc.pitch = m.pitch;
        } else {
            ++stat_mob_refused_;
            bc.valid = false; // Out-of-height move refused; re-sync next tick.
        }
    }
    // Water surface effects ride the same pass: the entry splash on the
    // transition, the rate-limited wake while the mob actually moves.
    waterEffects(m, brain, ctx, movedFlag, dx, dz);
    return true;
}

} // namespace cursestone::entity::ai
