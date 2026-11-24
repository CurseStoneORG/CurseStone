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
#include "entity/ai/MobAI.h"
#include "entity/EntityTypes.h"
#include "entity/ai/Goals.h"
#include "entity/mobs/MobKind.h"
#include <algorithm>
#include <cmath>

namespace cursestone::entity::ai {

void MobBrain::addGoal(std::unique_ptr<Goal> goal) {
    goals_.push_back(std::move(goal));
    active_.push_back(false);
    order_.push_back(goals_.size() - 1);
    // Selection visits goals from the highest priority down; ties keep
    // insertion order (stable_sort), which makes goal composition
    // deterministic for a given brain build.
    std::stable_sort(order_.begin(), order_.end(),
                     [this](size_t a, size_t b) {
                         return goals_[a]->priority() > goals_[b]->priority();
                     });
}

void MobBrain::stepThink(const MobContext& ctx) {
    clearIntent();

    // Release goals whose exit condition fired.
    for (size_t i = 0; i < goals_.size(); ++i) {
        if (active_[i] && !goals_[i]->shouldContinue(ctx)) {
            goals_[i]->stop();
            active_[i] = false;
        }
    }

    // Recompute the claimed flags from the still-active set.
    used_flags_ = 0;
    for (size_t i = 0; i < goals_.size(); ++i) {
        if (active_[i])
            used_flags_ |= goals_[i]->flags();
    }

    // Activate runnable goals in priority order, skipping any whose
    // flags overlap an already-claimed bit.
    for (size_t gi : order_) {
        if (active_[gi])
            continue;
        const uint8_t f = goals_[gi]->flags();
        if ((used_flags_ & f) != 0)
            continue;
        if (!goals_[gi]->canStart(ctx, *this))
            continue;
        goals_[gi]->start(ctx, *this);
        active_[gi] = true;
        used_flags_ |= f;
    }

    // Let every active goal write its intent. Overlapping flag claims
    // are impossible here, so goals never overwrite each other's field.
    for (size_t gi : order_) {
        if (active_[gi])
            goals_[gi]->tick(ctx, *this);
    }
}

float faceYaw(double dx, double dz) {
    // Protocol-84 convention, verified against Player::getDirectionVector
    // (x = -sin(yaw), z = cos(yaw)): a mob facing (dx, dz) gets
    // atan2(-dx, dz). atan2(+dx, dz) mirrors the facing across the Z axis
    // and would render the model walking backwards.
    return (float)(std::atan2(-dx, dz) * 180.0 / 3.14159265358979323846);
}

FacingStep resolveFacing(float body_yaw, float head_yaw, float pitch,
                         const MobIntent& intent, bool standing) {
    FacingStep out;
    out.bodyYaw = body_yaw;
    out.headYaw = head_yaw;
    out.pitch = pitch;

    if (intent.move) {
        // Travel owns the body: the model must face where it walks, or
        // every wander leg renders as a sideways/backward slide.
        out.bodyYaw = faceYaw(intent.dirX, intent.dirZ);
        // The head follows travel unless a look goal is active; the
        // look goal's head (and its pitch) wins while it runs.
        if (intent.look) {
            out.headYaw = intent.yaw;
            out.pitch = intent.pitch;
        } else {
            out.headYaw = out.bodyYaw;
            out.pitch = 0.0f;
        }
        return out;
    }

    if (intent.look) {
        // Standing: the head turns to the look target, the body squares
        // up to the head only past the follow angle and only at the
        // bounded per-tick step — a stance rotation, not a snap.
        out.headYaw = intent.yaw;
        out.pitch = intent.pitch;
        if (standing) {
            float d = intent.yaw - body_yaw;
            while (d > 180.0f)
                d -= 360.0f;
            while (d < -180.0f)
                d += 360.0f;
            const float abs_d = std::fabs(d);
            if (abs_d > kBodyFollowAngle) {
                const float step = std::copysign(std::fmin(abs_d, kBodyFollowStep), d);
                out.bodyYaw = body_yaw + step;
            }
        }
    }
    return out;
}

namespace water {

SwimBob swimBob(float phase) {
    const float s = std::sin(phase);
    return {kBobForce * s, -kBobPitchDeg * s};
}

float bobPhaseStep(float phase) {
    phase += kBobPhaseStep;
    // One step is far below 2*pi, so a single conditional subtract
    // keeps the phase in [0, 2*pi) forever — no fmod division per tick.
    if (phase >= kBobTwoPi)
        phase -= kBobTwoPi;
    return phase;
}

WaterVerticalStep waterVerticalStep(float vy, float submersion,
                                    bool head_in_water, bool move_intent,
                                    bool wants_jump, float bob_force) {
    if (wants_jump) {
        // Shore exit: a real hop against the bank, sized for water drag
        // so a one-block shore is climbable without overshooting.
        return {kWaterJumpOut, true};
    }
    // The stage-2 hold-band regulator. Outside the band a net force
    // accelerates the body back toward the half-submerged depth; inside
    // the band the flotation bob rides — a gentle zero-mean force that
    // rocks the body up and down over the surface, so a floating mob
    // never freezes on the waterline. The regulator still owns the band
    // edges, so the rocking can never walk the mob out of its float.
    const float err = submersion - kSubmergeTarget;
    float accel = -kWaterGravity; // Water gravity: the fall, at ease.
    if (head_in_water || err > kSubmergeBand) {
        // Under the hold depth, or the head is wet: paddle up.
        accel += kSwimPaddle;
    } else if (err >= -kSubmergeBand) {
        accel = bob_force; // Inside the hold band: the flotation bob.
    }
    if (move_intent && err > 0.0f) {
        // Forward strokes lift toward the hold depth, never above it —
        // a swimming mob crosses the water chest-deep, not skimming.
        accel += kSwimPaddle * 0.5f;
    }
    vy = (vy + accel) * kWaterDrag;
    if (vy > kWaterMaxRise)
        vy = kWaterMaxRise;
    if (vy < kWaterMaxSink)
        vy = kWaterMaxSink;
    return {vy, false};
}

} // namespace water

std::unique_ptr<MobBrain> makeMobBrain(uint8_t kind, int32_t type,
                                       uint64_t seed) {
    auto brain = std::make_unique<MobBrain>();
    brain->setKind(kind);
    mcp_random_seed(&brain->rng(), (uint32_t)(seed ^ (seed >> 32)));
    // Each floater rocks on its own beat: the bob phase is spread over
    // a full cycle from the identity seed, so a herd never bobs in
    // lockstep and the client never sees a synchronized wave.
    brain->swimPhase() =
        (float)((seed >> 8) & 0xFFFFu) * (water::kBobTwoPi / 65536.0f);
    // Stage 2 assembles brains from the species profile: every grounded
    // kind (Passive, Neutral and the land Hostiles) strolls, watches
    // nearby players and glances around; Passive kinds additionally
    // carry the panic reflex. Flight (ghast, blaze, bat), hopping
    // (slimes) and swimming (squid) keep the pre-AI static record — an
    // empty brain is never thought about, never moved, never sent
    // packets — until their own movement model lands.
    const entity::MobProfile profile = entity::mobProfile(type);
    if (!profile.grounded || profile.walk_speed <= 0.0f)
        return brain;
    const bool passive = kind == (uint8_t)mobs::MobKind::Passive;
    const bool hostile = kind == (uint8_t)mobs::MobKind::Hostile;
    brain->addGoal(std::make_unique<IdleLookGoal>());
    brain->addGoal(std::make_unique<LookAtPlayerGoal>(hostile));
    brain->addGoal(std::make_unique<WanderGoal>());
    if (passive)
        brain->addGoal(std::make_unique<PanicGoal>());
    return brain;
}

} // namespace cursestone::entity::ai
