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
// the main thread inside a wall-clock budget. The stage-2 pass is tiered:
// mobs near players get full physics every tick, mobs at the edge of the
// view distance update on a reduced cadence, and chunks with no player in
// range are never even visited (their records stay persisted in the
// region files). The actuation step converts brain intent into
// physics::sweep() moves and World::moveMob() broadcasts, with obstacle
// steering, the panic reflex, the half-submerged swim model and the
// water splash effects riding on top.
#pragma once
#include "CurseStone.h" // brings in the `using World = world::World;` alias
#include "entity/ai/MobAI.h"
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
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

    /// @brief One mob pass: player snapshot, active-chunk collection,
    ///        then the budgeted chunk-cursor pass over the active mobs.
    void tick(uint64_t current_tick);

    /// @brief Drops one mob's brain (the record is being removed). The
    ///        damage/removal paths call this so the brain map never
    ///        drifts from the record map — no periodic O(mobs) GC pass.
    void forget(int64_t eid);

    MobAIManager(const MobAIManager&) = delete;
    MobAIManager& operator=(const MobAIManager&) = delete;

    // ---- Tier contract (docs/MOB_AI.md section 5) ----
    // FULL    chunk within kFullTierRadius of a player: think on the
    //         staggered cadence, actuate every tick.
    // REDUCED within the view distance but beyond that radius:
    //         think on a longer cadence, actuate every kReducedInterval
    //         ticks (the stride per actuation is unchanged, so positions
    //         stay exact — packets just arrive at 5 Hz, which is what
    //         vanilla does to far entities anyway).
    // FROZEN  outside every player's view distance: the chunk is not
    //         visited at all; the record persists in the region files.
    static constexpr int32_t kFullTierRadius = 4;  ///< Chunks.
    static constexpr uint64_t kReducedInterval = 4; ///< Ticks.

  private:
    /// @brief A player position cached for one tick: the gate reads the
    ///        chunk coordinates, look goals read the world position.
    struct PlayerAnchor {
        double x = 0.0, y = 0.0, z = 0.0;
        int32_t cx = 0, cz = 0;
    };

    void buildPlayerSnapshot();
    /// @brief Rebuilds the visited-chunk list (every chunk within the
    ///        view distance of any player) when the player chunk set
    ///        changed or the short refresh timer elapsed. The ordered
    ///        key list drives the chunk cursor.
    void rebuildActiveChunks(uint64_t now);
    /// @brief Chebyshev chunk distance from a mob's chunk to the nearest
    ///        snapshot player, or INT32_MAX when players_ is empty.
    int32_t nearestPlayerChunkDist(int32_t mcx, int32_t mcz) const;
    /// @brief True when the cell at (x, y, z) is still or flowing water.
    bool isWaterCell(double x, double y, double z) const;
    /// @brief Y of the rendered waterline over the mob's column: the
    ///        topmost water block plus the era 14/16 surface offset.
    ///        Bounded upward scan from the feet cell; only called for
    ///        mobs already known to stand in water.
    float waterSurfaceY(double x, double y, double z) const;
    /// @brief Assembles the read-only input snapshot the goals see.
    ///        @p brain supplies the physics carry-over (on-ground) the
    ///        record does not carry.
    MobContext buildContext(const world::MobEntity& m, const MobBrain& brain,
                            uint64_t current_tick);
    /// @brief Think cadence for one mob: goals evaluate, intent lands
    ///        in the brain.
    void thinkOne(MobBrain& brain, const MobContext& ctx);
    /// @brief Intent -> steering -> physics -> record update + broadcast
    ///        + water effects. The only place AI state becomes a world
    ///        mutation.
    void actuateOne(world::MobEntity& m, MobBrain& brain,
                    const MobContext& ctx, uint64_t current_tick);
    /// @brief Obstacle steering: probes the cell one stride ahead and
    ///        rotates the intended direction toward the first open
    ///        candidate when a wall or deep water blocks it. Bounded
    ///        angles only, so the stride never snaps.
    /// @return the adjusted unit direction (same as the intent when the
    ///         path ahead is open).
    void steerAround(const world::MobEntity& m, const MobContext& ctx,
                     const MobIntent& intent, double& dir_x, double& dir_z);
    /// @brief Water effects of one actuation: the entry splash on the
    ///        air->water transition and the rate-limited swim wake while
    ///        the mob is actually moving through water.
    void waterEffects(world::MobEntity& m, MobBrain& brain,
                      const MobContext& ctx, bool moved, double move_dx,
                      double move_dz);

    World* world_;
    // Heap-allocated brains: the map is keyed by runtime id and brains
    // outlive rehashes; MobBrain itself is a moving bundle of goals and
    // intent the actuation step mutates through a stable reference.
    std::unordered_map<int64_t, std::unique_ptr<MobBrain>> brains_;
    std::vector<PlayerAnchor> players_;
    std::vector<physics::BlockBox> sweep_boxes_; ///< Scratch for collisionBoxes.
    // ---- Tier machinery ----
    std::vector<uint64_t> active_keys_;          ///< Player-distance ordered.
    uint64_t players_chunk_sig_ = 0;             ///< Player chunk layout sig.
    uint64_t active_refresh_tick_ = 0;           ///< Last rebuild tick.
    // ---- Pass diagnostics (one log line per window, real numbers for
    // operators and the benchmark harness: how much the pass actually
    // did and how often the budget cut it short). ----
    uint64_t stat_actuated_ = 0;
    uint64_t stat_thinks_ = 0;
    uint64_t stat_budget_stops_ = 0;
    uint64_t stat_window_tick_ = 0;
};

} // namespace entity::ai
} // namespace cursestone
