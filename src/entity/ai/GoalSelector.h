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
// GoalSelector — the era goal arbitration.
//
// Port of <minecraftpe/impl/entity/ai/GoalSelector.cpp> (MCPE 0.8.1
// decompile, oldminecraftcommunity), with two cleanups that keep the
// behavior byte-for-byte:
//   - InternalGoal entries live in one vector instead of leaked
//     heap nodes; ownership of each Goal is explicit (era's
//     InternalGoal::field_8 "delete the goal on destruction" flag).
//   - The era runningGoals list used find/erase on raw pointers; the
//     port keeps a parallel running vector of indices with swap-remove
//     semantics preserved as order-insensitive (the era list order was
//     itself unstable across stop/start cycles).
//
// Each MobBrain owns two selectors exactly like the era Mob: the
// target selector (goalSelector2) runs first, then the movement
// selector (goalSelector).
#pragma once
#include "entity/ai/Goal.h"
#include <cstddef>
#include <memory>
#include <vector>

namespace cursestone::entity::ai {

class Mob;

class GoalSelector {
  public:
    /// @brief Registers an owned goal: the selector deletes it.
    void addGoal(int32_t priority, std::unique_ptr<Goal> goal);

    /// @brief Registers a non-owned goal (the era addGoal(..., false):
    ///        the caller keeps the Goal object alive).
    void addGoal(int32_t priority, Goal& goal);

    /// @brief One arbitration pass: stops goals that lost their mutex or
    ///        finished, starts goals that can run, ticks the runners.
    ///        Mirrors GoalSelector::tick(), including its quirk that a
    ///        just-stopped goal is re-evaluated for a restart in the
    ///        same pass.
    void tick(Mob& mob);

    /// @brief True while @p goal is a running entry (tests/diagnostics).
    bool isRunning(const Goal* goal) const;

    ~GoalSelector();

  private:
    struct InternalGoal {
        int32_t priority = 0; ///< Era InternalGoal::field_4.
        Goal* goal = nullptr;
        bool owned = false; ///< Era InternalGoal::field_8.
        bool running = false; ///< Era InternalGoal::field_9.
    };

    /// @brief Era canCoExist: true when the two goals' control flags do
    ///        not overlap.
    static bool canCoExist(const InternalGoal& a, const InternalGoal& b) {
        return (a.goal->getRequiredControlFlags() &
                b.goal->getRequiredControlFlags()) == 0;
    }

    /// @brief Era canUseInSystem: @p candidate may start when every
    ///        running goal either shares no flags (equal-or-lower
    ///        priority case) or can be interrupted.
    bool canUseInSystem(const InternalGoal& candidate) const;

    std::vector<InternalGoal> goals_;
    std::vector<size_t> running_;  ///< Indices into goals_.
    std::vector<size_t> started_;  ///< Era field_18: goals to start().
};

} // namespace cursestone::entity::ai
