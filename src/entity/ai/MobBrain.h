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
// MobBrain — the per-mob AI bundle: the era Mob's two goal selectors,
// the five controls, the navigation and the facade the ported code
// drives, plus the physics carry-over the actuation step integrates.
//
// The era Mob wired exactly this set (minecraftpe/impl/entity/Mob.cpp):
// two selectors, sensing, navigation, move/look/jump controls, and the
// BodyControl reconciler. The brain reproduces that wiring once per mob
// and exposes the era newServerAiStep pipeline.
#pragma once
#include "CurseStone.h" // `using World = world::World;`
#include "entity/ai/BodyControl.h"
#include "entity/ai/GoalSelector.h"
#include "entity/ai/JumpControl.h"
#include "entity/ai/LookControl.h"
#include "entity/ai/Mob.h"
#include "entity/ai/MoveControl.h"
#include "entity/ai/PathNavigation.h"
#include "entity/ai/Sensing.h"
#include <memory>

namespace cursestone {
namespace world {
struct MobEntity;
}
namespace entity {
namespace ai {

/// @brief Shove speed below which a consumed knockback stops sliding.
constexpr float kKnockbackStop = 0.02f;

class MobBrain {
  public:
    /// @param type the protocol-84 registry id (selects the goal set).
    /// @param seed diversifies the per-mob randomness.
    MobBrain(int32_t type, uint64_t seed);

    /// @brief One era newServerAiStep pass: idle counter, target
    ///        selector, movement selector, navigation, the mob-specific
    ///        step (the creeper fuse), then move/look/jump/body controls.
    void newServerAiStep(World* world, world::MobEntity& record,
                         uint64_t tick);

    /// @brief True when the mob carries behaviors: static kinds keep the
    ///        pre-AI behavior (no physics, no packets).
    bool dynamic() const { return has_goals_; }

    Mob& host() { return host_; }
    const Mob& host() const { return host_; }

    /// @brief The era constructor goal registrations, per mob type. The
    ///        factory in makeMobBrain calls it right after construction.
    void setupGoals(int32_t type, uint8_t kind);

    /// @brief The creeper fuse finished: the actuation must detonate and
    ///        remove the record.
    bool wantsExplode() const { return wants_explode_; }

    // ---- Physics carry-over (integrated by the actuation step; the
    // same channels the previous CurseStone brain carried) ----
    float& motionY() { return motion_y_; }
    float motionY() const { return motion_y_; }
    bool& wantsJump() { return wants_jump_; }
    int& jumpCooldown() { return jump_cooldown_; }
    float& motionX() { return motion_x_; }
    float& motionZ() { return motion_z_; }
    bool hasKnockback() const { return motion_x_ != 0.0f || motion_z_ != 0.0f; }
    /// @brief Applies one friction tick; sub-epsilon values snap to zero.
    void decayKnockback(float friction) {
        motion_x_ *= friction;
        motion_z_ *= friction;
        if (motion_x_ > -kKnockbackStop && motion_x_ < kKnockbackStop)
            motion_x_ = 0.0f;
        if (motion_z_ > -kKnockbackStop && motion_z_ < kKnockbackStop)
            motion_z_ = 0.0f;
    }
    bool& inWater() { return in_water_; }
    bool inWater() const { return in_water_; }
    int& splashCooldown() { return splash_cd_; }
    bool& sprintFlag() { return sprint_on_; }
    bool sprintFlag() const { return sprint_on_; }

    // ---- Move-intent latch (reduced-tier physics carrier) ----
    // The era pipeline ran every tick, so the stride MoveControl armed
    // was consumed the same tick. The reduced tier runs the goal pass
    // every kReducedInterval ticks, but the actuation (and with it the
    // vanilla -0.08/0.98 fall integration) must run every tick, or far
    // mobs fall at a quarter speed. The latch holds the last armed
    // (yaw, stride) between goal passes; it refreshes on every armed
    // goal tick and expires after kMoveLatchTicks without one, so a
    // finished goal stops driving the body within the same window the
    // era hard stop took.
    struct MoveLatch {
        float yaw = 0.0f;    ///< The era movement-yaw channel.
        float stride = 0.0f; ///< Blocks per tick the goal armed.
        uint32_t ttl = 0;    ///< Ticks left before the intent expires.
    };
    MoveLatch& moveLatch() { return move_latch_; }
    const MoveLatch& moveLatch() const { return move_latch_; }

    // ---- Broadcast cache (actuation bookkeeping) ----
    // The last position/rotation handed to World::moveMob; a mob that
    // changed less than the packet thresholds sends nothing.
    struct BroadcastCache {
        bool valid = false;
        double x = 0.0, y = 0.0, z = 0.0;
        float yaw = 0.0f;      ///< Body yaw (the move packet channel).
        float head_yaw = 0.0f; ///< Head yaw.
        float pitch = 0.0f;    ///< Head pitch.
    };
    BroadcastCache& broadcast() { return broadcast_; }

  private:
    /// @brief The creeper's server-side fuse (the era Creeper::tick body
    ///        the decompile lost the tail of): swellTime climbs while
    ///        the swell direction is armed and detonates at 30.
    void creeperServerStep();

    int32_t type_;
    Mob host_;
    GoalSelector targets_; ///< Era goalSelector2.
    GoalSelector goals_;   ///< Era goalSelector.
    LookControl look_control_;
    MoveControl move_control_;
    BodyControl body_control_;
    JumpControl jump_control_;
    Sensing sensing_;
    PathNavigation navigation_;
    bool has_goals_ = false;
    bool wants_explode_ = false;

    float motion_y_ = 0.0f;
    float motion_x_ = 0.0f, motion_z_ = 0.0f;
    bool wants_jump_ = false;
    int jump_cooldown_ = 0;
    bool in_water_ = false;
    int splash_cd_ = 0;
    bool sprint_on_ = false;
    MoveLatch move_latch_;
    BroadcastCache broadcast_;
};

/// @brief Builds the brain of a mob type: the era goal set for the mobs
///        the 0.8.1 decompile registers, the closest family set for the
///        rest (documented in MobBrain.cpp), and an empty (static) brain
///        for the kinds this server does not simulate (flight, hopping,
///        aquatic).
std::unique_ptr<MobBrain> makeMobBrain(int32_t type, uint64_t seed);

} // namespace ai
} // namespace entity
} // namespace cursestone
