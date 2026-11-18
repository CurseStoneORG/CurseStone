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
// Goal — one composable behavior of a mob brain, plus the MobContext
// snapshot a goal is allowed to read. Goals are pure decision logic:
// they never touch the World, players or packets. They read the
// immutable MobContext and write movement intent into their MobBrain;
// the MobAIManager is the only component that turns intent into world
// mutations (see docs/MOB_AI.md).
#pragma once
#include <cstdint>

namespace cursestone::entity::ai {

/// @brief Mutex bits a goal claims while it runs. A goal cannot start
///        while another active goal holds an overlapping bit, which
///        keeps mutually exclusive behaviors (moving vs. looking around)
///        from fighting over the same output.
enum GoalFlags : uint8_t {
    kNoFlag = 0,
    kFlagMove = 1 << 0, ///< Controls horizontal movement intent.
    kFlagLook = 1 << 1, ///< Controls the facing yaw intent.
};

/// @brief The immutable input snapshot one goal evaluation may read.
///
/// Deliberately value-only: no pointers into mutable world state, so a
/// future off-thread thinker can consume the same struct unchanged
/// (docs/MOB_AI.md section 6).
struct MobContext {
    int64_t eid = 0;
    int32_t type = 0; ///< Protocol-84 network id (entity::EntityTypes).
    uint8_t kind = 0; ///< Behavior family as the raw MobKind value.
    double x = 0.0, y = 0.0, z = 0.0; ///< Feet position.
    float yaw = 0.0f;
    bool onGround = false;
    bool playerNear = false;       ///< A spawned player is within look range.
    double playerDist = 0.0;       ///< Horizontal distance to that player.
    double playerX = 0.0, playerY = 0.0, playerZ = 0.0;
    uint64_t tick = 0;             ///< Current server tick.
};

/// @brief One behavior of a mob brain.
///
/// A goal object is owned by exactly one MobBrain and may keep its own
/// state between evaluations (wander targets, timers). Selection runs
/// on the mob's think cadence: goals with a higher priority() win flag
/// conflicts, canStart() gates activation, shouldContinue() decides
/// when to release.
class Goal {
  public:
    virtual ~Goal() = default;

    /// @brief Mutex bits claimed while this goal runs.
    virtual uint8_t flags() const {
        return kNoFlag;
    }

    /// @brief Selection weight; higher wins an overlapping flag.
    virtual int priority() const {
        return 0;
    }

    /// @brief Returns whether the goal may activate on this evaluation.
    ///        May consume @p brain's randomness (per-mob, deterministic
    ///        on the main thread).
    virtual bool canStart(const MobContext& ctx, class MobBrain& brain) = 0;

    /// @brief Returns whether an active goal keeps running.
    virtual bool shouldContinue(const MobContext& ctx) const = 0;

    /// @brief Called once when the goal activates; may initialize its
    ///        own state (and consume @p brain's randomness).
    virtual void start(const MobContext& ctx, class MobBrain& brain) {
        (void)ctx;
        (void)brain;
    }

    /// @brief Called once when the goal deactivates.
    virtual void stop() {}

    /// @brief Runs on every think tick while the goal is active. Writes
    ///        intent into @p brain (see MobIntent).
    virtual void tick(const MobContext& ctx, class MobBrain& brain) = 0;
};

} // namespace cursestone::entity::ai
