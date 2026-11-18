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
// Stage-1 passive behaviors (docs/MOB_AI.md section 8): WanderGoal,
// LookAtPlayerGoal and IdleLookGoal. Each goal is a self-contained
// class with its own timers; together they make a passive mob stroll,
// face nearby players and glance around when bored.
#pragma once
#include "entity/ai/Goal.h"

namespace cursestone::entity::ai {

/// @brief Calm wander pace in blocks per tick (~1.1 blocks per second).
constexpr float kWanderSpeed = 0.055f;

/// @brief Picks a random nearby point and walks to it, then rests.
///
/// Claims the MOVE flag: while wandering, no other goal may drive
/// movement. Facing is left to the actuation step (face the movement
/// direction) unless a LOOK goal overrides it.
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
/// nearby player always wins the facing decision.
class LookAtPlayerGoal : public Goal {
  public:
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
    static constexpr double kStartRange = 8.0;  ///< Horizontal blocks.
    static constexpr double kStopRange = 10.0;  ///< Hysteresis blocks.
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

} // namespace cursestone::entity::ai
