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
// Stage-2 behaviors (docs/MOB_AI.md section 8): WanderGoal,
// LookAtPlayerGoal, IdleLookGoal and the passive PanicGoal. Each goal
// is a self-contained class with its own timers; together they make a
// grounded mob stroll at its species pace, face nearby players, glance
// around when bored, and bolt away from whatever just hit it.
#pragma once
#include "entity/ai/Goal.h"

namespace cursestone::entity::ai {

/// @brief How long a passive mob keeps fleeing after the last hit (the
///        vanilla panic window: roughly five seconds). A fresh hit
///        re-arms the window, so a mob under continuous attack keeps
///        running until the beating stops.
constexpr uint64_t kPanicTicks = 100;

/// @brief Picks a random nearby point and walks to it, then rests.
///
/// Claims the MOVE flag: while wandering, no other goal may drive
/// movement. Facing is left to the actuation step (face the movement
/// direction) unless a LOOK goal overrides it. The stride speed is the
/// species stroll from the context profile, so a cow trots and a
/// chicken scurries with no per-goal tuning.
class WanderGoal : public Goal {
  public:
    uint8_t flags() const override {
        return kFlagMove;
    }
    int priority() const override {
        return 1;
    }
    bool canStart(const MobContext& ctx, MobBrain& brain) override;
    bool shouldContinue(const MobContext& ctx) const override;
    void start(const MobContext& ctx, MobBrain& brain) override;
    void tick(const MobContext& ctx, MobBrain& brain) override;
    void stop() override {
        has_target_ = false;
    }

  private:
    bool has_target_ = false;
    double target_x_ = 0.0, target_z_ = 0.0;
    uint64_t expires_tick_ = 0;
};

/// @brief Faces a nearby player while one is within look range.
///
/// Claims the LOOK flag with a priority above IdleLookGoal, so a
/// nearby player always wins the facing decision. Hostile kinds get a
/// longer stare range (the vanilla mobs track players well before
/// melee range); the ranges land in the instance, not in constants,
/// because the goal object is per-brain anyway.
class LookAtPlayerGoal : public Goal {
  public:
    /// @param hostile true gives the longer hostile stare range.
    explicit LookAtPlayerGoal(bool hostile = false)
        : start_range_(hostile ? 12.0 : 8.0), stop_range_(hostile ? 14.0 : 10.0) {
    }
    uint8_t flags() const override {
        return kFlagLook;
    }
    int priority() const override {
        return 2;
    }
    bool canStart(const MobContext& ctx, MobBrain& brain) override;
    bool shouldContinue(const MobContext& ctx) const override;
    void tick(const MobContext& ctx, MobBrain& brain) override;

  private:
    double start_range_; ///< Horizontal blocks to begin the stare.
    double stop_range_;  ///< Hysteresis blocks to drop it.
};

/// @brief Glances in a random direction now and then.
///
/// Lowest LOOK priority: runs only when no player is worth facing.
class IdleLookGoal : public Goal {
  public:
    uint8_t flags() const override {
        return kFlagLook;
    }
    int priority() const override {
        return 1;
    }
    bool canStart(const MobContext& ctx, MobBrain& brain) override;
    bool shouldContinue(const MobContext& ctx) const override;
    void start(const MobContext& ctx, MobBrain& brain) override;
    void tick(const MobContext& ctx, MobBrain& brain) override;
    void stop() override {
        expires_tick_ = 0;
    }

  private:
    uint64_t expires_tick_ = 0;
    float yaw_ = 0.0f;
};

/// @brief Flees from the threat that just landed a hit.
///
/// Passive-only: the manager stamps every hit's away-from-damager
/// direction into the mob record from the damage pipeline (any thread),
/// and this goal reads it through the context snapshot. Claims MOVE at
/// a priority above Wander, so a mob that was strolling when struck
/// bolts instead — and keeps running for @c kPanicTicks after the last
/// hit, re-aimed every so often so it does not run in a straight line
/// into a wall.
class PanicGoal : public Goal {
  public:
    PanicGoal() = default;
    uint8_t flags() const override {
        return kFlagMove;
    }
    int priority() const override {
        return 3;
    }
    bool canStart(const MobContext& ctx, MobBrain& brain) override;
    bool shouldContinue(const MobContext& ctx) const override;
    void tick(const MobContext& ctx, MobBrain& brain) override;
    void stop() override {
        reaim_tick_ = 0;
    }

  private:
    double aim_x_ = 0.0, aim_z_ = 0.0;
    uint64_t reaim_tick_ = 0;
};

} // namespace cursestone::entity::ai
