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
#include <memory>
#include <vector>

namespace cursestone::entity::ai {

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
    float yaw = 0.0f;         ///< Facing in protocol-84 degrees.
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
        intent_.look = true;
        intent_.yaw = yaw;
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
    // The last position/yaw handed to World::moveMob. The actuation
    // step compares against it and skips sub-threshold moves, so an
    // idle mob produces no packets at all.
    struct BroadcastCache {
        bool valid = false;
        double x = 0.0, y = 0.0, z = 0.0;
        float yaw = 0.0f;
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

} // namespace cursestone::entity::ai
