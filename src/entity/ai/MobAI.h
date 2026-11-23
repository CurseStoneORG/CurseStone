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
// MobBrain — every per-mob dynamic AI state in one place: the owned
// goal list, the movement intent the active goals produced, and the
// physics carry-over the actuation step integrates. A brain holds NO
// pointers into the world; goals and actuation communicate only
// through these fields, so state mutations stay on the main thread
// (docs/MOB_AI.md).
#pragma once
#include "entity/ai/Goal.h"
#include "utils/Random.h"
#include <cmath>
#include <memory>
#include <vector>

namespace cursestone::entity::ai {

/// @brief Shove speed below which a consumed knockback stops sliding.
constexpr float kKnockbackStop = 0.02f;

/// @brief The movement decision the goals produced on one think tick.
///
/// Filled by goals, consumed by the actuation step of MobAIManager.
/// Both sides never touch anything else, which is the calculation /
/// mutation separation of docs/MOB_AI.md.
struct MobIntent {
    bool move = false;        ///< Horizontal movement requested.
    double dirX = 0.0;        ///< Unit direction of that movement.
    double dirZ = 0.0;
    float speed = 0.0f;       ///< Blocks per tick while moving.
    bool look = false;        ///< Explicit facing requested.
    float yaw = 0.0f;         ///< Head facing in protocol-84 degrees.
    float pitch = 0.0f;       ///< Head pitch in protocol-84 degrees
                              ///< (positive = down), so a mob tilts its
                              ///< head toward a player above or below.
};

/// @brief Per-mob AI state machine.
class MobBrain {
  public:
    /// @brief Adds a goal; the brain owns it. Selection order is
    ///        priority-descending among goals added so far.
    void addGoal(std::unique_ptr<Goal> goal);

    /// @brief One goal-selection pass: releases finished goals, starts
    ///        runnable ones respecting the flag mutex, then lets every
    ///        active goal write its intent.
    void stepThink(const MobContext& ctx);

    const MobIntent& intent() const {
        return intent_;
    }

    /// @brief Drops any previously produced intent (start of a think).
    void clearIntent() {
        intent_ = MobIntent{};
    }

    void setMoveIntent(double dirX, double dirZ, float speed) {
        intent_.move = true;
        intent_.dirX = dirX;
        intent_.dirZ = dirZ;
        intent_.speed = speed;
    }

    void setLookIntent(float yaw) {
        setLookIntent(yaw, 0.0f);
    }

    /// @brief Looks at @p yaw with the head tilted to @p pitch.
    void setLookIntent(float yaw, float pitch) {
        intent_.look = true;
        intent_.yaw = yaw;
        intent_.pitch = pitch;
    }

    // ---- Physics carry-over (integrated by the actuation step) ----
    float& motionY() {
        return motion_y_;
    }
    float motionY() const {
        return motion_y_;
    }
    bool& onGround() {
        return on_ground_;
    }
    bool onGround() const {
        return on_ground_;
    }
    /// @brief Jump requested by the actuation step after a horizontal
    ///        clip; consumed (and cleared) on the next integration.
    bool& wantsJump() {
        return wants_jump_;
    }
    int& jumpCooldown() {
        return jump_cooldown_;
    }

    // ---- Knockback carry-over (decayed by the actuation step) ----
    // Horizontal shove written when a hit's record knockback is consumed;
    // the actuation adds it to the sweep, decays it every tick and zeroes
    // it below the stop epsilon, so a mob keeps sliding briefly and stops.
    float& motionX() {
        return motion_x_;
    }
    float& motionZ() {
        return motion_z_;
    }
    /// @brief True while a consumed shove still carries motion.
    bool hasKnockback() const {
        return motion_x_ != 0.0f || motion_z_ != 0.0f;
    }
    /// @brief Applies one friction tick (the era ground knockback decay);
    ///        values under the stop epsilon snap to zero.
    void decayKnockback(float friction) {
        motion_x_ *= friction;
        motion_z_ *= friction;
        if (motion_x_ > -kKnockbackStop && motion_x_ < kKnockbackStop)
            motion_x_ = 0.0f;
        if (motion_z_ > -kKnockbackStop && motion_z_ < kKnockbackStop)
            motion_z_ = 0.0f;
    }

    // ---- Protocol-84 identity (copied from the mob record) ----
    void setKind(uint8_t kind) {
        kind_ = kind;
    }
    uint8_t kind() const {
        return kind_;
    }

    /// @brief Returns true when the brain owns behaviors, i.e. the mob
    ///        is dynamic. Static kinds (Water, Ambient, and kinds that
    ///        have no behavior stage yet) keep the pre-AI behavior:
    ///        no physics, no packets.
    bool dynamic() const {
        return !goals_.empty();
    }

    mcp_random_t& rng() {
        return rng_;
    }

    // ---- Broadcast cache (actuation bookkeeping) ----
    // The last position/rotation handed to World::moveMob. The actuation
    // step compares against it and skips sub-threshold moves, so an
    // idle mob produces no packets at all.
    struct BroadcastCache {
        bool valid = false;
        double x = 0.0, y = 0.0, z = 0.0;
        float yaw = 0.0f;       ///< Body yaw.
        float head_yaw = 0.0f;  ///< Head yaw (independent of the body).
        float pitch = 0.0f;     ///< Head pitch.
    };
    BroadcastCache& broadcast() {
        return broadcast_;
    }

  private:
    std::vector<std::unique_ptr<Goal>> goals_;
    std::vector<size_t> order_; ///< Indices into goals_, priority-descending.
    std::vector<bool> active_;
    uint8_t used_flags_ = 0;    ///< Union of the active goals' flags.
    uint8_t kind_ = 0;

    MobIntent intent_;

    // Physics carry-over for the actuation step.
    float motion_y_ = 0.0f;
    bool on_ground_ = false;
    bool wants_jump_ = false;
    int jump_cooldown_ = 0;

    // Knockback carry-over (horizontal; the vertical pop reuses the
    // wants_jump_ hop the step-up logic already integrates).
    float motion_x_ = 0.0f;
    float motion_z_ = 0.0f;

    BroadcastCache broadcast_;

    mcp_random_t rng_{};
};

/// @brief Builds the brain of a mob kind: the stage-1 passive goal set
///        (wander, look at player, idle) for Passive kinds, an empty
///        brain for everything not covered by a behavior stage yet.
///        @p seed diversifies per-mob randomness.
std::unique_ptr<MobBrain> makeMobBrain(uint8_t kind, uint64_t seed);

/// @brief Protocol-84 facing for a direction: the same atan2(dx, dz)
///        convention the arrow spawn path uses.
float faceYaw(double dx, double dz);

/// @brief The rotation one actuation pass broadcasts: body yaw, head
///        yaw and head pitch. The 0.15.10 client renders the body from
///        the move packet's yaw and the head from its headYaw, so the
///        two are independent channels — a mob walking one way must
///        keep its body in the movement direction even while its head
///        tracks a player (body and head in one yaw reads as
///        moonwalking: the model faces the player and slides sideways).
struct FacingStep {
    float bodyYaw = 0.0f;
    float headYaw = 0.0f;
    float pitch = 0.0f;
};

/// @brief Head-to-body angle above which a standing mob starts turning
///        its body toward its head (the vanilla stance behavior: a mob
///        that watches you long enough squares up).
constexpr float kBodyFollowAngle = 45.0f;

/// @brief Body-yaw degrees one tick may close toward the head while
///        standing; bounds the stance turn so it reads as a rotation,
///        not a snap.
constexpr float kBodyFollowStep = 5.0f;

/// @brief Resolves one facing step. @p body_yaw / @p head_yaw /
///        @p pitch are the record's current values; @p intent carries
///        the goals' decision; @p standing marks a mob with no
///        movement intent (the only state where the body may turn for
///        anything but travel).
FacingStep resolveFacing(float body_yaw, float head_yaw, float pitch,
                         const MobIntent& intent, bool standing);

/// @brief Vanilla-era water buoyancy per tick (the +0.04 the era
///        reference entity model adds while swimming).
constexpr float kWaterBuoyancy = 0.04f;

/// @brief Vertical drag inside water (motion shrinks to 80% per tick).
constexpr float kWaterDrag = 0.8f;

/// @brief Extra upward paddle while swimming: applied when the head is
///        still under the surface or the mob is swimming somewhere.
constexpr float kSwimUpBoost = 0.06f;

/// @brief Rise/sink clamps inside water. Mobs bob to the surface and
///        stop; they never rocket up and never sink away.
constexpr float kWaterMaxRise = 0.12f;
constexpr float kWaterMaxSink = -0.15f;

/// @brief The shore-exit hop: a horizontal clip against a bank while
///        swimming spends this as an upward jump so the mob can climb
///        out (the vanilla paddling-against-the-shore hop).
constexpr float kWaterJumpOut = 0.30f;

/// @brief One vertical step of the swim model. Pure so the water check
///        harness can verify the surface bob, the sink clamp and the
///        shore hop without a World.
struct WaterVerticalStep {
    float vy = 0.0f;          ///< Integrated vertical motion.
    bool jumpConsumed = false; ///< True when @p wants_jump was spent.
};

WaterVerticalStep waterVerticalStep(float vy, bool head_in_water,
                                    bool move_intent, bool wants_jump);

} // namespace cursestone::entity::ai
