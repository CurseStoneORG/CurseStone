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
// MobAIManager — the per-world driver of the mob AI pass (docs/MOB_AI.md).
// Owned by its World and drained from World::tick(), it runs entirely on
// the main thread inside a wall-clock budget: presence gating, the
// staggered think cadence, the round-robin pass and the actuation step
// that converts brain intent into physics::sweep() moves and
// World::moveMob() broadcasts.
#pragma once
#include "CurseStone.h" // brings in the `using World = world::World;` alias
#include "entity/ai/MobAI.h"
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace cursestone {
namespace world {
struct MobEntity;
}
namespace physics {
struct BlockBox;
}
namespace entity::ai {

class MobAIManager {
  public:
    /// @param owner the world whose mobs this manager drives; must
    ///        outlive the manager (World owns it as a member).
    explicit MobAIManager(World* owner);

    /// @brief One mob pass: player snapshot, brain GC, then the
    ///        budgeted round-robin over the world's mobs.
    void tick(uint64_t current_tick);

    MobAIManager(const MobAIManager&) = delete;
    MobAIManager& operator=(const MobAIManager&) = delete;

  private:
    /// @brief A player position cached for one tick: the gate reads the
    ///        chunk coordinates, look goals read the world position.
    struct PlayerAnchor {
        double x = 0.0, y = 0.0, z = 0.0;
        int32_t cx = 0, cz = 0;
    };

    void buildPlayerSnapshot();
    /// @brief True when any snapshot player is within the world's view
    ///        distance (Chebyshev chunk distance) of the mob's chunk.
    bool gateOpen(int32_t mcx, int32_t mcz) const;
    /// @brief True when the cell at (x, y, z) is still or flowing water.
    ///        The swim model reads three cells per mob per tick (feet,
    ///        chest, head) through this probe.
    bool isWaterCell(double x, double y, double z) const;
    /// @brief Assembles the read-only input snapshot the goals see.
    ///        @p brain supplies the physics carry-over (on-ground) the
    ///        record does not carry.
    MobContext buildContext(const world::MobEntity& m, const MobBrain& brain,
                            uint64_t current_tick);
    /// @brief Think cadence for one mob: goals evaluate, intent lands
    ///        in the brain.
    void thinkOne(MobBrain& brain, const MobContext& ctx);
    /// @brief Intent -> physics -> record update + broadcast. The only
    ///        place AI state becomes a world mutation.
    void actuateOne(world::MobEntity& m, MobBrain& brain,
                    const MobContext& ctx);

    World* world_;
    // Heap-allocated brains: the map is keyed by runtime id and brains
    // outlive rehashes; MobBrain itself is a moving bundle of goals and
    // intent the actuation step mutates through a stable reference.
    std::unordered_map<int64_t, std::unique_ptr<MobBrain>> brains_;
    std::vector<PlayerAnchor> players_;
    std::vector<physics::BlockBox> sweep_boxes_; ///< Scratch for collisionBoxes.
    size_t cursor_ = 0; ///< Round-robin position of the next pass.
};

} // namespace entity::ai
} // namespace cursestone
