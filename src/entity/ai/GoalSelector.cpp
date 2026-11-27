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
#include "entity/ai/GoalSelector.h"
#include "entity/ai/Mob.h"
#include <algorithm>

namespace cursestone::entity::ai {

void GoalSelector::addGoal(int32_t priority, std::unique_ptr<Goal> goal) {
    InternalGoal entry;
    entry.priority = priority;
    entry.goal = goal.release();
    entry.owned = true;
    entry.running = false;
    goals_.push_back(entry);
}

void GoalSelector::addGoal(int32_t priority, Goal& goal) {
    InternalGoal entry;
    entry.priority = priority;
    entry.goal = &goal;
    entry.owned = false;
    entry.running = false;
    goals_.push_back(entry);
}

bool GoalSelector::canUseInSystem(const InternalGoal& candidate) const {
    for (const InternalGoal& g : goals_) {
        if (&g == &candidate || !g.running)
            continue;
        // Era logic: a candidate with equal-or-higher priority must not
        // share flags with a runner; a lower-priority candidate must be
        // able to interrupt the runner.
        if (candidate.priority >= g.priority) {
            if (!canCoExist(candidate, g))
                return false;
        } else if (!g.goal->canInterrupt()) {
            return false;
        }
    }
    return true;
}

bool GoalSelector::isRunning(const Goal* goal) const {
    for (const size_t idx : running_) {
        if (goals_[idx].goal == goal)
            return true;
    }
    return false;
}

void GoalSelector::tick(Mob& mob) {
    started_.clear();

    const size_t count = goals_.size();
    for (size_t i = 0; i < count; ++i) {
        InternalGoal& g = goals_[i];

        // Era tick(): a running goal that lost its mutex or finished is
        // stopped first, and the very same pass immediately re-tries to
        // start it again (canUseInSystem + canUse).
        if (!g.running) {
            if (canUseInSystem(g) && g.goal->canUse(mob)) {
                g.running = true;
                started_.push_back(i);
                running_.push_back(i);
            }
            continue;
        }
        if (!canUseInSystem(g) || !g.goal->canContinueToUse(mob)) {
            g.goal->stop(mob);
            g.running = false;
            const auto it = std::find(running_.begin(), running_.end(), i);
            if (it != running_.end())
                running_.erase(it);
            if (canUseInSystem(g) && g.goal->canUse(mob)) {
                g.running = true;
                started_.push_back(i);
                running_.push_back(i);
            }
        }
    }

    for (const size_t idx : started_)
        goals_[idx].goal->start(mob);
    for (const size_t idx : running_)
        goals_[idx].goal->tick(mob);
}

GoalSelector::~GoalSelector() {
    for (InternalGoal& g : goals_) {
        if (g.owned)
            delete g.goal;
    }
}

} // namespace cursestone::entity::ai
