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
// Mob — the AI-layer facade the ported MCPE 0.8.1 code operates on.
//
// The era decompile drives real Mob entity objects (posX, yaw, headYaw,
// boundingBox, level, random ...). CurseStone mobs are plain
// world::MobEntity records driven by MobAIManager, so this facade
// reproduces the era Mob surface on top of the record plus the per-mob
// dynamic state the brain owns. One Mob object lives inside its
// MobBrain; bind() attaches it to the record being simulated this tick.
//
// Yaw convention: all angles are the protocol-84 entity yaw the server
// already broadcasts (0 = +Z, 90 = +X; direction = (sin, cos)). See
// AiMath.h for the era conversion note.
#pragma once
#include "utils/Random.h"
#include <cstdint>
#include <memory>

namespace cursestone {
namespace world {
struct MobEntity;
class World;
}
namespace entity {
namespace ai {

class LookControl;
class MoveControl;
class BodyControl;
class JumpControl;
class Sensing;
class PathNavigation;
class GoalSelector;
class MobDamagerProxy;

/// @brief A resolved snapshot of a player or mob a goal tracks. The era
///        code held TempEPtr smart references across ticks; CurseStone
///        identifies targets by eid and refreshes the snapshot through
///        Mob::resolve() at every use, so a removed victim degrades to
///        an empty reference instead of a dangling pointer.
struct TargetRef {
    int64_t eid = 0; ///< Runtime id, 0 = no target.
    bool player = false;
    double x = 0.0, y = 0.0, z = 0.0; ///< Feet position.
    float width = 0.6f;               ///< Body width.
    float height = 1.8f;              ///< Body height.
    float eyeHeight = 1.62f;          ///< Eye line above the feet.
    float yaw = 0.0f;                 ///< Facing (the TemptGoal scare test).
    float pitch = 0.0f;
    bool alive = false;

    void clear() { *this = TargetRef{}; }
    explicit operator bool() const { return eid != 0; }

    /// @brief Squared horizontal+vertical distance to a point, the era
    ///        Entity::distanceToSqr(x, y, z) shape.
    double distanceToSqr(double px, double py, double pz) const {
        const double dx = px - x, dy = py - y, dz = pz - z;
        return dx * dx + dy * dy + dz * dz;
    }

    /// @brief Squared distance to another resolved reference.
    double distanceToSqr(const TargetRef& other) const {
        return distanceToSqr(other.x, other.y, other.z);
    }

    /// @brief Squared distance to a point measured with the other body's
    ///        feet Y clamped to its box bottom (era distanceToSqr(entity)
    ///        uses the bounding-box centers, so both sides reduce the
    ///        vertical term by half a height; the AI call sites only
    ///        compare against ranges, and this shape matches the melee
    ///        goal's era measurement against the victim's box bottom).
    double distanceToSqrBody(const TargetRef& other) const {
        return distanceToSqr(other.x, other.y, other.z);
    }
};

/// @brief The era Mob surface the goals and controls call.
class Mob {
  public:
    Mob() = default;
    /// @brief Releases the lazily-built attacker proxy.
    ~Mob();

    /// @brief Attaches the facade to the record simulated this tick.
    ///        @p world may be null in the pure-logic harnesses, which
    ///        only exercise the controls; every world query degrades to
    ///        its false/empty answer then.
    void bind(world::MobEntity& record, world::World* world, uint64_t tick);

    // ---- Identity ----
    int64_t eid() const;
    int32_t type() const;
    world::World* level() const { return world_; }
    uint64_t tick() const { return tick_; }

    // ---- Position / rotation channels (the era entity fields) ----
    double posX() const;
    double posY() const;
    double posZ() const;
    /// @brief The movement yaw MoveControl steers (era Mob::yaw).
    float yaw() const { return move_yaw_; }
    void setYaw(float yaw) { move_yaw_ = yaw; }
    /// @brief The body rotation (era Mob::field_124), broadcast as the
    ///        move packet's yaw channel.
    float bodyYaw() const;
    void setBodyYaw(float yaw);
    float headYaw() const;
    void setHeadYaw(float yaw);
    float pitch() const;
    void setPitch(float pitch);

    /// @brief Seeds the movement-yaw channel from the persisted body
    ///        yaw. Called on the first bind of a fresh brain.
    void seedRotation(float bodyYaw) {
        move_yaw_ = bodyYaw;
        prev_x_ = 0.0;
        prev_z_ = 0.0;
        prev_valid_ = false;
    }

    // ---- BodyControl's per-tick displacement inputs ----
    double prevX() const { return prev_x_; }
    double prevZ() const { return prev_z_; }
    bool prevValid() const { return prev_valid_; }
    /// @brief Stores the position the next BodyControl tick compares
    ///        against (the actuation's last integrated position).
    void setPrevPos(double x, double z) {
        prev_x_ = x;
        prev_z_ = z;
        prev_valid_ = true;
    }

    // ---- Body model ----
    /// @brief Eye height above the feet (era Mob::getHeadHeight).
    float getHeadHeight() const;
    float entityWidth() const;
    float entityHeight() const;
    /// @brief The era base speed attribute: the species stroll stride in
    ///        blocks per tick, scaled by the Speed/Slowness effect.
    float getBaseSpeed() const;
    void setSpeed(float blocksPerTick) { speed_ = blocksPerTick; }
    float speed() const { return speed_; }
    /// @brief Era Mob::getMaxHeadXRot: the pitch cap setLookAt receives.
    float getMaxHeadXRot() const { return 40.0f; }

    // ---- State the controls drive ----
    void setJumping(bool jumping) { jumping_ = jumping; }
    bool isJumping() const { return jumping_; }
    bool onGround() const { return on_ground_; }
    /// @brief The actuation step reports the sweep's ground contact
    ///        here; PathNavigation::canUpdatePath reads it.
    void setOnGround(bool grounded) { on_ground_ = grounded; }
    /// @brief True when MoveControl armed a speed toward a wanted
    ///        position this tick (the actuation's move channel).
    void setMoveTick(bool moving) { moving_tick_ = moving; }
    bool movingTick() const { return moving_tick_; }
    bool isInWater() const;
    bool isInLava() const;
    bool isAlive() const;
    /// @brief Era mobs have no baby stage on this server.
    bool isBaby() const { return false; }

    // ---- Targeting ----
    /// @brief Resolves the attack target live (the era getTarget).
    TargetRef getTarget() const;
    void setTarget(int64_t eid, bool player) {
        target_eid_ = eid;
        target_player_ = player;
    }
    int64_t targetEid() const { return target_eid_; }
    bool targetIsPlayer() const { return target_player_; }
    /// @brief The era getLastHurtByMob: non-empty for the panic window
    ///        after a hit, from the record's cross-thread stamp.
    TargetRef getLastHurtByMob() const;
    /// @brief Refreshes @p ref from the world; clears it when the entity
    ///        is gone or dead.
    void resolve(TargetRef& ref) const;
    /// @brief The era Level::getNearestPlayer(mob, range): the closest
    ///        spawned, alive player of this world within @p range (a
    ///        negative range matches any distance).
    TargetRef nearestPlayer(double range) const;

    // ---- Combat ----
    /// @brief The era melee hit: applies the mob's attack damage to the
    ///        victim (player or mob) with the era knockback the damage
    ///        pipeline produces. @return true when damage was applied.
    bool doHurtTarget(const TargetRef& victim);
    /// @brief The skeleton shot: era Arrow(level, holder, target, 1.6,
    ///        12.0) — velocity toward the victim's eye line with the era
    ///        gaussian inaccuracy, registered in the world's arrow set.
    void fireArrowAt(const TargetRef& target);

    // ---- Creeper swell state (era Creeper fields) ----
    int32_t getSwellDir() const { return swell_dir_; }
    void setSwellDir(int32_t dir) { swell_dir_ = dir; }
    int32_t swellTime = 0;    ///< Era Creeper::swellTime (0..30).
    int32_t prevSwellTime = 0; ///< Era prevSwellTime (render smoothing).

    // ---- Control accessors (the era Mob::getXxxControl) ----
    /// @brief The brain wires these before the first AI tick; they stay
    ///        valid for the brain's lifetime.
    void setNavigation(PathNavigation* nav) { navigation_ = nav; }
    PathNavigation* getNavigation() const { return navigation_; }
    void setSensing(Sensing* sensing) { sensing_ = sensing; }
    Sensing* getSensing() const { return sensing_; }
    void setLookControl(LookControl* look) { look_control_ = look; }
    LookControl* getLookControl() const { return look_control_; }
    void setMoveControl(MoveControl* move) { move_control_ = move; }
    MoveControl* getMoveControl() const { return move_control_; }
    void setJumpControl(JumpControl* jump) { jump_control_ = jump; }
    JumpControl* getJumpControl() const { return jump_control_; }
    void setBodyControl(BodyControl* body) { body_control_ = body; }
    BodyControl* getBodyControl() const { return body_control_; }

    /// @brief Era Entity::distanceToSqr(x, y, z).
    double distanceToSqr(double x, double y, double z) const {
        const double dx = x - posX();
        const double dy = y - posY();
        const double dz = z - posZ();
        return dx * dx + dy * dy + dz * dz;
    }

    /// @brief Era Mob::isOnFire: no mob burning exists on this server,
    ///        so FleeSunGoal stays inert until that lands.
    bool isOnFire() const { return false; }

    /// @brief Era Level::isDay: the world clock's day half.
    bool isDay() const;

    /// @brief Era Level::canSeeSky: a bounded column scan.
    bool canSeeSky(int32_t x, int32_t y, int32_t z) const;

    /// @brief Era Mob::isCollidedHorizontally, fed by the actuation's
    ///        sweep result (the door goals read it).
    bool isCollidedHorizontally() const { return collided_h_; }
    void setCollidedHorizontally(bool collided) { collided_h_ = collided; }

    // ---- Idle counter (era Mob::noActionTime) ----
    /// @brief Increments per AI tick; the era checkDespawn reset keeps a
    ///        mob near a player strolling. @see resetNoActionTime.
    void incNoActionTime() { ++no_action_time_; }
    int32_t getNoActionTime() const { return no_action_time_; }
    /// @brief The era checkDespawn reset: a player within 32 blocks
    ///        (distance-squared 1024) zeroes the counter.
    void resetNoActionTimeIfPlayerNear() {
        const TargetRef p = nearestPlayer(-1.0);
        if (p && p.distanceToSqr(posX(), posY(), posZ()) <= 1024.0)
            no_action_time_ = 0;
    }

    mcp_random_t& random() { return random_; }

  private:
    world::MobEntity* rec_ = nullptr;
    world::World* world_ = nullptr;
    uint64_t tick_ = 0;

    // Movement-yaw channel (era Mob::yaw): rotated by MoveControl,
    // chased by BodyControl; never broadcast directly.
    float move_yaw_ = 0.0f;
    double prev_x_ = 0.0, prev_z_ = 0.0;
    bool prev_valid_ = false;

    float speed_ = 0.0f;
    bool jumping_ = false;
    bool on_ground_ = false;
    bool collided_h_ = false;
    bool moving_tick_ = false;

    int64_t target_eid_ = 0;
    bool target_player_ = false;

    // Lazy attacker stand-in for the damage pipeline (built on the
    // first melee hit, kept for the brain's lifetime). Raw pointer:
    // MobDamagerProxy stays forward-declared here, the out-of-line
    // destructor releases it.
    MobDamagerProxy* proxy_ = nullptr;

    int32_t swell_dir_ = -1;
    int32_t no_action_time_ = 0;

    // Back-wired components (owned by MobBrain, non-null after wiring).
    PathNavigation* navigation_ = nullptr;
    Sensing* sensing_ = nullptr;
    LookControl* look_control_ = nullptr;
    MoveControl* move_control_ = nullptr;
    JumpControl* jump_control_ = nullptr;
    BodyControl* body_control_ = nullptr;

    mcp_random_t random_{};
};

} // namespace ai
} // namespace entity
} // namespace cursestone
