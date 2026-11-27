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
// MobAIManager — the per-world driver of the mob AI pass (docs/MOB_AI.md).
// Owned by its World and drained from World::tick(), it runs entirely on
// the main thread inside a wall-clock budget. The pass is tiered: mobs
// near players run the full era pipeline every tick (MobBrain::
// newServerAiStep + actuation), mobs at the edge of the view distance
// update on a reduced cadence, and chunks with no player in range are
// never visited (their records stay persisted in the region files).
// The actuation step converts the era control state — movement yaw,
// speed channel, jumping — into physics::sweep() moves and
// World::moveMob() broadcasts, with the water splash effects riding on
// top.
#pragma once
#include "CurseStone.h" // brings in the `using World = world::World;` alias
#include "entity/ai/MobBrain.h"
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace cursestone {
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

    // ---- Tier contract ----
    // FULL    chunk within kFullTierRadius of a player: the full era
    //         pipeline (goals + controls + actuation) every tick.
    // REDUCED within the view distance but beyond that radius: the goal
    //         pipeline runs every kReducedInterval ticks while the
    //         actuation (physics + broadcasts) runs EVERY tick, so the
    //         vanilla fall integration and the movement stream stay at
    //         20 Hz — the expensive A*/sensing work is what the cadence
    //         throttles, not the physics. The stride MoveControl armed
    //         persists across the skipped passes through the brain's
    //         move latch.
    // FROZEN  outside every player's view distance: the chunk is not
    //         visited at all; the record persists in the region files.
    static constexpr int32_t kFullTierRadius = 4;  ///< Chunks.
    static constexpr uint64_t kReducedInterval = 4; ///< Ticks.

    /// @brief The per-tick environment inputs the actuation reads (the
    ///        old MobContext subset that survives the era port).
    struct PlayerContext {
        bool inWater = false;
        bool headInWater = false;
        float waterSurface = 0.0f;
        bool panicActive = false;
        uint64_t tick = 0;
    };

  private:
    /// @brief A player position cached for one tick: the gate reads the
    ///        chunk coordinates, the goals read the world position.
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
    /// @brief Intent -> physics -> record update + broadcast + water
    ///        effects. The only place AI state becomes a world mutation.
    ///        @param goal_ran true when the era goal pipeline ran for
    ///        this mob this tick; between reduced-tier goal passes the
    ///        actuation still runs so the vanilla fall integration
    ///        advances every tick, carrying the latched move intent.
    ///        @return false when the record must not receive physics
    ///        this tick (unresident chunk, void).
    bool actuateOne(world::MobEntity& m, MobBrain& brain,
                    const PlayerContext& ctx, uint64_t current_tick,
                    bool goal_ran);
    /// @brief Water effects of one actuation: the entry splash on the
    ///        air->water transition and the rate-limited swim wake while
    ///        the mob is actually moving through water.
    void waterEffects(world::MobEntity& m, MobBrain& brain,
                      const PlayerContext& ctx, bool moved, double move_dx,
                      double move_dz);

  private:
    World* world_;
    // Heap-allocated brains: the map is keyed by runtime id and brains
    // outlive rehashes; MobBrain itself is a moving bundle of selectors,
    // controls and physics channels the actuation mutates through a
    // stable reference.
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
    uint64_t stat_moves_ = 0;       ///< Actuations that carried movement.
    uint64_t stat_mob_moved_ = 0;   ///< Accepted moveMob broadcasts.
    uint64_t stat_mob_refused_ = 0; ///< moveMob refusals.
    uint64_t stat_budget_stops_ = 0;
    uint64_t stat_window_tick_ = 0;
};

} // namespace entity::ai
} // namespace cursestone
