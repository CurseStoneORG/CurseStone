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
// PathFinder — the bounded A* the 0.8.1 navigation walks.
//
// Port of <minecraftpe/impl/entity/path/PathFinder.cpp> (MCPE 0.8.1
// decompile). The search is 4-connected (no diagonals), walks at most
// one cell of climb per step, and never searches beyond the follow
// range. Faithful ports of the era quirks:
//   - a failed search still returns the partial path to the best node
//     reached (the era reconstruct toward the closest-to-goal node);
//   - the water flag: a cell inside water is a swim node (2) when the
//     mob may swim, or blocked (-1) when avoidWater is set;
//   - lava below the mob is deadly (-2), fences and trapdoors never
//     climb (-3 / -4).
//
// Adaptation: the era node cache leaked on every search (the decompile
// clears the map without deleting). The port owns nodes through
// unique_ptr entries cleared per search, and keys the cache with the
// full 64-bit cell (the era 32-bit hash collided outside a +-127 range).
#pragma once
#include "entity/ai/path/BinaryHeap.h"
#include "entity/ai/path/Path.h"
#include "entity/ai/path/PathNode.h"
#include <cstdint>
#include <memory>
#include <unordered_map>

namespace cursestone {
namespace world {
class World;
}
namespace entity {
namespace ai {

class Mob;

/// @brief The world query surface the search needs. The server binds
///        WorldPathSource; the pure-logic harness binds a tile grid.
class IPathSource {
  public:
    virtual ~IPathSource() = default;
    virtual uint8_t tileAt(int32_t x, int32_t y, int32_t z) const = 0;
    /// @brief True when a walker may share the cell (non-solid).
    virtual bool isPathfindable(uint8_t id) const = 0;
    virtual bool isWater(uint8_t id) const = 0;
    virtual bool isLava(uint8_t id) const = 0;
    virtual bool isWoodDoor(uint8_t id) const = 0;
    virtual bool isFenceOrGate(uint8_t id) const = 0;
    virtual bool isTrapdoor(uint8_t id) const = 0;
};

/// @brief The server-side source: World tiles plus the collision table.
class WorldPathSource final : public IPathSource {
  public:
    explicit WorldPathSource(world::World* world) : world_(world) {}
    /// @brief Re-points the source (the navigation binds per tick).
    void setWorld(world::World* world) { world_ = world; }
    uint8_t tileAt(int32_t x, int32_t y, int32_t z) const override;
    bool isPathfindable(uint8_t id) const override;
    bool isWater(uint8_t id) const override;
    bool isLava(uint8_t id) const override;
    bool isWoodDoor(uint8_t id) const override;
    bool isFenceOrGate(uint8_t id) const override;
    bool isTrapdoor(uint8_t id) const override;

  private:
    world::World* world_;
};

/// @brief The era body-box in cells (isFree scans this volume).
struct PathBox {
    int32_t x = 1, y = 2, z = 1;
};

class PathFinder {
  public:
    /// @brief Era ctor flags (PathFinder(LevelSource*, canPassDoors,
    ///        canOpenDoors, avoidWater, canFloat)).
    PathFinder(IPathSource* source, bool canPassDoors, bool canOpenDoors,
               bool avoidWater, bool canFloat)
        : source_(source), can_pass_doors_(canPassDoors),
          can_open_doors_(canOpenDoors), avoid_water_(avoidWater),
          can_float_(canFloat) {}

    // The navigation toggles the flags at runtime (RestrictSunGoal /
    // FloatGoal), the era setters did the same on PathNavigation and the
    // finder was rebuilt; the port keeps one finder with live flags.
    void setCanFloat(bool v) { can_float_ = v; }
    void setAvoidWater(bool v) { avoid_water_ = v; }
    bool avoidWater() const { return avoid_water_; }
    bool canFloat() const { return can_float_; }
    bool canOpenDoors() const { return can_open_doors_; }

    /// @brief The full search: entity body -> goal cell within @p range.
    ///        Returns nullptr when even the start is unusable.
    std::unique_ptr<Path> findPath(const Mob& mob, double tx, double ty,
                                   double tz, float range);

  private:
    std::unique_ptr<Path> findPath(const Mob& mob, const PathNode& start,
                                   const PathNode& goal,
                                   const PathBox& box, float range);
    int32_t getNeighbors(const Mob& mob, const PathNode& current,
                         const PathNode& goal, const PathBox& box,
                         float range);
    PathNode* getNode(int32_t x, int32_t y, int32_t z);
    PathNode* getNodeClimb(const Mob& mob, int32_t x, int32_t y, int32_t z,
                           const PathBox& box, int32_t climb);
    /// @brief The era ascent check: a candidate cell stands on ground
    ///        within four cells below (never water with avoidWater, never
    ///        lava); returns the accepted (possibly lowered) node.
    PathNode* ascendCheck(const Mob& mob, PathNode* node, int32_t y,
                          int32_t x, int32_t z, const PathBox& box);
    /// @brief The era isFree: scans the body box; 1 = walkable,
    ///        2 = swim node, -1/-2/-3/-4 = water/lava/fence/trapdoor
    ///        rejections, 0 = solid.
    int32_t isFree(const Mob& mob, int32_t x, int32_t y, int32_t z,
                   const PathBox& box) const;
    std::unique_ptr<Path> reconstruct(const PathNode* start,
                                      const PathNode* end) const;

    IPathSource* source_;
    bool can_pass_doors_;
    bool can_open_doors_;
    bool avoid_water_;
    bool can_float_;

    BinaryHeap heap_;
    PathNode neighbor_nodes_[4];
    int32_t neighbor_count_ = 0;
    // Per-search node cache: era nodes.find(hash); the port owns the
    // nodes and clears them when the search ends.
    std::unordered_map<uint64_t, std::unique_ptr<PathNode>> nodes_;
};

} // namespace ai
} // namespace entity
} // namespace cursestone
