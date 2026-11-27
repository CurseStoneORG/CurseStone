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
// PathNavigation — the bridge between the goals and MoveControl.
//
// Port of <minecraftpe/impl/entity/path/PathNavigation.cpp> (MCPE 0.8.1
// decompile). Goals call moveTo(entity/position, speed); tick() skips
// the path ahead while the body rides it, feeds the current cell into
// MoveControl::setWantedPosition, and abandons a path that made no
// progress for 100 ticks.
//
// Adaptations for the server (behavior-preserving, cost-saving):
//   - the full A* re-run a moveTo request triggers is rate-limited: a
//     re-path only happens when the path is gone or finished, the goal
//     moved more than 1.5 blocks from the last computed target, or the
//     path aged past kRepathTicks (the era re-pathed on every request;
//     the goals themselves re-request every few ticks, which on a
//     server-sized population turns A* into the dominant AI cost);
//   - the canMoveDirectly shortcut (era PathNavigation::canMoveDirectly)
//     stays: a straight-line walkable segment lets the path index jump
//     ahead.
#pragma once
#include "entity/ai/Mob.h"
#include "entity/ai/path/PathFinder.h"
#include <cstdint>
#include <memory>

namespace cursestone::entity::ai {

class Mob;

class PathNavigation {
  public:
    /// @brief Maximum ticks between two A* re-runs while the same goal
    ///        keeps being requested (the re-path rate limit).
    static constexpr int32_t kRepathTicks = 40;

    PathNavigation(Mob* mob, world::World* level, float range)
        : mob_(mob), level_(level), range_(range), source_(level),
          finder_(&source_,
                                                           /*canPassDoors=*/true,
                                                           /*canOpenDoors=*/false,
                                                           /*avoidWater=*/false,
                                                           /*canFloat=*/false) {}
    /// @brief Out-of-line: Path is only forward-declared for the holder.
    ~PathNavigation();

    // ---- Era flags ----
    void setCanFloat(bool canFloat) { finder_.setCanFloat(canFloat); }
    void setAvoidWater(bool avoid) { finder_.setAvoidWater(avoid); }
    void setAvoidSun(bool avoid) { avoid_sun_ = avoid; }
    bool getAvoidWater() const { return finder_.avoidWater(); }
    bool canOpenDoors() const { return finder_.canOpenDoors(); }
    bool canFloat() const { return finder_.canFloat(); }

    /// @brief Points the tile source at @p level (the brain binds the
    ///        world on every AI tick; the era PathNavigation::setLevel).
    void bind(world::World* level) {
        level_ = level;
        source_.setWorld(level);
    }

    // ---- Era query surface ----
    Path* getPath() const { return path_.get(); }
    bool isDone() const { return !path_ || path_->isDone(); }

    /// @brief Era moveTo(entity, speed) with the re-path rate limit.
    bool moveToEntity(const TargetRef& target, float speed);
    /// @brief Era moveTo(x, y, z, speed) with the re-path rate limit.
    bool moveTo(double x, double y, double z, float speed);
    /// @brief Era moveTo(Path*, speed): adopts a computed path.
    bool moveToPath(std::unique_ptr<Path> path, float speed);

    void stop() {
        path_.reset();
        speed_multiplier_ = 0.0f;
        last_goal_valid_ = false;
    }

    /// @brief Era tick(): index skip-ahead, straight-line shortcut, the
    ///        stuck check, then the current cell into MoveControl.
    void tick();

  private:
    /// @brief Era canUpdatePath: the body is grounded, or floating in a
    ///        liquid with canFloat.
    bool canUpdatePath() const;

    /// @brief Era updatePath: advances the index past the cells the body
    ///        already covers, jumps to the farthest directly-reachable
    ///        cell, and stops a 100-tick no-progress path.
    void updatePath();

    /// @brief Era trimPathFromSun: with avoidSun set, a path starting
    ///        under cover is truncated at its first sky-exposed cell.
    void trimPathFromSun();

    /// @brief A bounded column scan: no solid cell above the probe.
    bool skyOpen(int32_t x, int32_t y, int32_t z) const;

    /// @brief The era getTempMobPos height: the walk surface cell (the
    ///        water top when the body floats).
    int32_t getSurfaceY() const;

    /// @brief Era canMoveDirectly: true when the straight segment stays
    ///        walkable for the whole body.
    bool canMoveDirectly(double sx, double sy, double sz, double ex,
                         double ey, double ez, int32_t w, int32_t h,
                         int32_t d) const;
    /// @brief Era canWalkOn: the column box at a cell is non-solid with
    ///        a solid floor (the forward-axis cells only).
    bool canWalkOn(int32_t x, int32_t y, int32_t z, int32_t w, int32_t h,
                   int32_t d, double ox, double oz, double ux,
                   double uz) const;

    Mob* mob_;
    world::World* level_;
    float range_;
    WorldPathSource source_;
    PathFinder finder_;

    std::unique_ptr<Path> path_;
    float speed_multiplier_ = 0.0f;

    // Re-path rate-limit state.
    double last_goal_x_ = 0.0, last_goal_y_ = 0.0, last_goal_z_ = 0.0;
    bool last_goal_valid_ = false;
    int32_t last_path_tick_ = -100000;

    // Era stuck-detection state (field_18 / field_1C / field_20).
    int32_t age_ticks_ = 0;
    int32_t last_progress_age_ = 0;
    double last_progress_x_ = 0.0, last_progress_y_ = 0.0,
           last_progress_z_ = 0.0;

    bool avoid_sun_ = false;
};

} // namespace cursestone::entity::ai
