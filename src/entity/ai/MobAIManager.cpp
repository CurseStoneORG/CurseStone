/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___/ \__,_|

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
#include "entity/ai/Goals.h"
#include "math/AxisAlignedBB.h"
#include "math/Vector3.h"
#include "physics/Physics.h"
#include "utils/MainLogger.hpp"
#include "world/Position.h"
#include "world/World.h"
#include "world/particle/WaterSplashParticle.h"
#include "world/particle/WaterWakeParticle.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
namespace cursestone::entity::ai {
namespace {

// Budgets and cadences (docs/MOB_AI.md section 4). The budget mirrors
// the light engine's contract: under load mobs slow down, the server
// does not. The base budget grows with the population (bounded well
// under the tick budget) so a herd near the players keeps its smooth
// per-tick simulation instead of starving the moment a few thousand
// mobs are loaded.
constexpr double kMobBudgetMs = 2.0;      ///< Wall-clock cap, small worlds.
constexpr double kMobBudgetMaxMs = 8.0;   ///< Absolute wall-clock cap.
constexpr double kMobBudgetPerMob = 1.0 / 2000.0; ///< Extra ms per record.
constexpr uint64_t kThinkInterval = 10; ///< Ticks between goal passes.
constexpr uint64_t kBrainGCInterval = 200; ///< Brains drift sweep cadence.
constexpr double kLookRange = 8.0;      ///< Player range the goals see.

// Movement integration constants, matching the era entity model the
// default Entity tick and the item integrator already use.
constexpr float kGravity = 0.08f;
constexpr float kDrag = 0.98f;
constexpr float kJumpVelocity = 0.42f;
constexpr float kMaxFallSpeed = -3.92f;
constexpr int kJumpCooldownTicks = 10;
constexpr double kJumpProbe = 0.7;  ///< Horizontal probe distance for steps.
constexpr double kSteerProbe = 0.6; ///< Horizontal probe distance for walls.

// Steering candidates, degrees off the intended direction, nearest turn
// first. A wall ahead reads as a turn of at most 135 degrees — never a
// snap to a random bearing — so a strolling mob flows around obstacles
// instead of grinding into them or zig-zagging.
constexpr double kSteerAngles[] = {30.0, -30.0, 60.0, -60.0,
                                   90.0, -90.0, 135.0, -135.0};

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

// Water effects. The wake rides behind a mob that is actually moving
// through water (never while it floats still), rate limited to one
// particle burst every few ticks; the entry splash fires once on the
// air->water transition when the mob fell in with real speed.
constexpr int kWakeIntervalTicks = 4;
constexpr float kEntrySplashMinFall = 0.15f;

// How fresh a hit must be for the reflex to fire the same tick (the
// panic goal starts on the next actuation pass, at most one tick after
// the strike lands, instead of waiting for the think cadence).
constexpr uint64_t kPanicFreshTicks = 2;

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
    // bounded: bodies this stage floats never stand under more than a
    // couple of stacked water blocks, and a deep column would only cost
    // a few extra reads once before the buoyancy lifts the mob out.
    int32_t cell = (int32_t)std::floor(y);
    for (int i = 0; i < 8; ++i) {
        if (!waterId(world_->getBlockId((int32_t)std::floor(x), cell + 1,
                                        (int32_t)std::floor(z))))
            break;
        ++cell;
    }
    return (float)cell + kWaterSurfaceOffset;
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

    // Species profile: speeds, body box and eye height ride the context,
    // so goals never hardcode a species constant.
    const entity::MobProfile profile = entity::mobProfile(m.type);
    ctx.walkSpeed = profile.walk_speed;
    ctx.panicSpeed = profile.panic_speed;
    ctx.bodyWidth = profile.width;
    ctx.bodyHeight = profile.height;
    ctx.eyeHeight = profile.eye_height;

    // Water snapshot for the swim model: the feet and chest cells make
    // the body buoyant, the head cell decides the surfacing paddle.
    ctx.inWater = isWaterCell(m.x, m.y, m.z) || isWaterCell(m.x, m.y + 0.5, m.z);
    ctx.headInWater = isWaterCell(m.x, m.y + 1.5, m.z);
    if (ctx.inWater)
        ctx.waterSurface = waterSurfaceY(m.x, m.y, m.z);

    // Panic: the damage pipeline stamps the hit tick and the away-from-
    // threat direction under the mob lock (hits can arrive from any
    // thread); the reflex stays armed for the vanilla panic window and
    // re-arms on every fresh hit.
    if (m.panic_tick != 0 && current_tick >= m.panic_tick &&
        current_tick - m.panic_tick <= kPanicTicks) {
        ctx.panicActive = true;
        ctx.panicDirX = m.panic_dir_x;
        ctx.panicDirZ = m.panic_dir_z;
    }

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

void MobAIManager::steerAround(const world::MobEntity& m, const MobContext& ctx,
                               const MobIntent&, double& dir_x,
                               double& dir_z) {
    const int feet_y = (int)std::floor(m.y);

    // A probe cell blocks the stride when a wall (two or more body
    // levels of solid) stands in it, or — for a mob on land — when deep
    // water starts there. A single solid level is a step, which the
    // jump logic climbs; a one-deep ford is walkable. Land mobs steer
    // away from ponds deeper than their hold depth instead of diving.
    const auto blocked = [&](double dx, double dz) {
        const int bx = (int)std::floor(m.x + dx * kSteerProbe);
        const int bz = (int)std::floor(m.z + dz * kSteerProbe);
        const uint8_t feet = world_->getBlockId(bx, feet_y, bz);
        if (World::entityCollidable(feet))
            return World::entityCollidable(world_->getBlockId(bx, feet_y + 1, bz));
        if (!ctx.inWater && waterId(feet))
            return waterId(world_->getBlockId(bx, feet_y - 1, bz));
        return false;
    };

    if (!blocked(dir_x, dir_z))
        return;
    const double base = std::atan2(dir_z, dir_x);
    for (const double deg : kSteerAngles) {
        const double rad = base + deg * (3.14159265358979323846 / 180.0);
        const double cx = std::cos(rad);
        const double cz = std::sin(rad);
        if (!blocked(cx, cz)) {
            dir_x = cx;
            dir_z = cz;
            return;
        }
    }
    // Every candidate blocked: keep the course. The sweep stops the
    // body, the jump logic spends its cooldown, and the next think
    // picks a fresh target — a stuck tick, not a stuck mob.
}

void MobAIManager::waterEffects(world::MobEntity& m, MobBrain& brain,
                                const MobContext& ctx, bool moved,
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
    // pass (population, live brains, actuations, thinks, budget cuts).
    // A pass that never hits the budget reports zero cuts; under heavy
    // load the cuts show the far-chunk degradation instead of a stall.
    const auto log_stats = [this, &pass_start](uint64_t now) {
        if (now % kBrainGCInterval != 0)
            return;
        char line[192];
        std::snprintf(line, sizeof(line),
                      "[MOB AI] records=%zu brains=%zu actuated=%llu thinks=%llu "
                      "budget_cuts=%llu chunks=%zu",
                      world_->mobCount(), brains_.size(),
                      (unsigned long long)stat_actuated_,
                      (unsigned long long)stat_thinks_,
                      (unsigned long long)stat_budget_stops_,
                      active_keys_.size());
        utils::MainLogger::get().info(line);
        stat_actuated_ = 0;
        stat_thinks_ = 0;
        stat_budget_stops_ = 0;
        stat_window_tick_ = now;
    };
    const size_t chunk_count = active_keys_.size();
    // Budgeted distance-ordered pass: chunks nearest the players are
    // visited first every tick, the walk runs outward until the
    // wall-clock budget is spent. Under light populations the whole
    // active set completes each tick; under heavy ones the degradation
    // lands on the outermost chunks (held pose, resumed when the
    // budget allows), never on the mobs a player is watching. Mob
    // records are snapshot per chunk (small vectors under the mob
    // lock), so the pass never materializes the whole population and
    // never holds the lock while physics or packets run.
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
                                  makeMobBrain((uint8_t)entity::mobTypeKind(m->type),
                                               m->type, (uint64_t)m->eid))
                         .first;
                it->second->broadcast().valid = false;
                return *it->second;
            }();

            if (!brain.dynamic()) {
                // Static kind (flight, hops, aquatic): exactly the
                // pre-AI behavior — no physics, no packets.
                continue;
            }

            MobContext ctx = buildContext(*m, brain, current_tick);

            // Tier: full physics near players, reduced cadence at the
            // edge of the view distance. A panicking mob always runs
            // full: the flight must read smoothly at any distance.
            const int32_t player_dist = nearestPlayerChunkDist(m->cx, m->cz);
            const bool full_tier =
                player_dist <= kFullTierRadius || ctx.panicActive;
            if (!full_tier) {
                const uint64_t phase =
                    (current_tick + (uint64_t)m->eid % kReducedInterval) %
                    kReducedInterval;
                if (phase != 0)
                    continue;
            }

            // Think cadence, staggered by entity id so a herd never
            // re-decides in lockstep. A fresh hit fires an extra think
            // right now — the reflex must not wait half a second.
            const uint64_t fresh =
                m->panic_tick != 0 && current_tick >= m->panic_tick &&
                current_tick - m->panic_tick <= kPanicFreshTicks
                    ? 1
                    : 0;
            const uint64_t interval =
                (full_tier ? kThinkInterval : kThinkInterval * kReducedInterval);
            if (fresh ||
                (current_tick + (uint64_t)m->eid % kThinkInterval) % interval == 0) {
                thinkOne(brain, ctx);
                ++stat_thinks_;
            }
            actuateOne(*m, brain, ctx, current_tick);
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

void MobAIManager::actuateOne(world::MobEntity& m, MobBrain& brain,
                              const MobContext& ctx, uint64_t) {
    // Unloaded terrain would read as air and drop the mob through the
    // world; the chunk-cursor pass only visits active chunks, but a mob
    // can sit in a chunk a viewer already left — this guard keeps
    // physics on resident chunks only (the same rule world.setBlock
    // follows).
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

    // ---- Steering + facing: the body follows where the mob will
    // actually walk this tick. A raw intent that grinds into a wall
    // while the model faces it reads as a stuck replay; the steered
    // direction owns the facing exactly like the raw one owns it when
    // the path ahead is open.
    double dir_x = intent.dirX;
    double dir_z = intent.dirZ;
    if (intent.move)
        steerAround(m, ctx, intent, dir_x, dir_z);
    MobIntent facing_intent = intent;
    facing_intent.dirX = dir_x;
    facing_intent.dirZ = dir_z;
    const ai::FacingStep facing = ai::resolveFacing(
        m.yaw, m.head_yaw, m.pitch, facing_intent,
        /*standing=*/brain.onGround());

    // Sprint pose: a fleeing passive carries the FLAGS sprint bit, the
    // pose and the faster leg cycle the 0.15.10 client already renders
    // for sprinting entities. Toggle only on state change.
    if (ctx.panicActive != brain.sprintFlag()) {
        brain.sprintFlag() = ctx.panicActive;
        world_->setMobSprinting(m.eid, m.cx, m.cz, ctx.panicActive);
    }

    // ---- Swim-vs-walk: the hold-band model owns the body only where
    // water is deep enough to submerge the hold depth (or the feet are
    // off the bottom); a ford shallower than that is walked, with the
    // era water slowdown on the stride.
    const float submersion =
        ctx.inWater
            ? (float)((ctx.waterSurface - m.y) / std::max(ctx.bodyHeight, 0.1f))
            : 0.0f;
    const bool swimming =
        ctx.inWater &&
        (submersion > ai::water::kSubmergeTarget - ai::water::kSubmergeBand ||
         !brain.onGround());

    const bool idle =
        !intent.move && !brain.hasKnockback() && brain.onGround() &&
        !brain.wantsJump() && !swimming;
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
        waterEffects(m, brain, ctx, false, 0.0, 0.0);
        return;
    }

    // ---- Physics integration (the item/falling-block sweep shape) ----
    float& vy = brain.motionY();
    if (swimming) {
        // The hold-band swim model: gravity, the hold band, the paddle
        // and the shore hop integrate to a mob resting half-submerged —
        // the waterline mid-body, not the soles riding the surface.
        const ai::water::WaterVerticalStep water = ai::water::waterVerticalStep(
            vy, submersion, ctx.headInWater, intent.move, brain.wantsJump());
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
        dx = dir_x * stride;
        dz = dir_z * stride;
    }
    // A consumed shove rides on top of the goal's own step.
    dx += (double)brain.motionX();
    dz += (double)brain.motionZ();

    const double half_w = ctx.bodyWidth * 0.5;
    const mcp_aabb_t bb = mcp_aabb_make(m.x - half_w, m.y, m.z - half_w,
                                        m.x + half_w, m.y + ctx.bodyHeight,
                                        m.z + half_w);
    const mcp_aabb_t lookup = mcp_aabb_clone_move(bb, dx, vy, dz);
    world_->collisionBoxes(lookup, sweep_boxes_);

    physics::MotionResult res = physics::sweep(bb, mcp_v3(dx, vy, dz), sweep_boxes_);
    const double nx = m.x + res.motion.x;
    const double ny = m.y + res.motion.y;
    const double nz = m.z + res.motion.z;
    brain.onGround() = res.on_ground;
    brain.decayKnockback(kKnockbackFriction);

    // Step-up: a horizontal clip against a one-block step (headroom
    // above it) is worth a jump; a clipped column at chest or head
    // height is a wall, and jumping would only grind against it — the
    // steering already walked around walls, this catches the ledges.
    // In water the same clip is a shore: the pending jump becomes the
    // swim hop that climbs the bank (no onGround requirement — a
    // swimming mob has none).
    if ((res.clipped_x || res.clipped_z) && intent.move &&
        brain.jumpCooldown() <= 0) {
        if (swimming) {
            brain.wantsJump() = true;
            brain.jumpCooldown() = kJumpCooldownTicks;
        } else if (res.on_ground) {
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
    // Water surface effects ride the same pass: the entry splash on the
    // transition, the rate-limited wake while the mob actually moves.
    waterEffects(m, brain, ctx, moved, dx, dz);
}

} // namespace cursestone::entity::ai
