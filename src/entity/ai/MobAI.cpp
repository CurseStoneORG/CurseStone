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
#include "entity/ai/MobAI.h"
#include "entity/ai/Goals.h"
#include "entity/mobs/MobKind.h"
#include <algorithm>
#include <cmath>

namespace cursestone::entity::ai {

void MobBrain::addGoal(std::unique_ptr<Goal> goal) {
    goals_.push_back(std::move(goal));
    active_.push_back(false);
    order_.push_back(goals_.size() - 1);
    // Selection visits goals from the highest priority down; ties keep
    // insertion order (stable_sort), which makes goal composition
    // deterministic for a given brain build.
    std::stable_sort(order_.begin(), order_.end(),
                     [this](size_t a, size_t b) {
                         return goals_[a]->priority() > goals_[b]->priority();
                     });
}

void MobBrain::stepThink(const MobContext& ctx) {
    clearIntent();

    // Release goals whose exit condition fired.
    for (size_t i = 0; i < goals_.size(); ++i) {
        if (active_[i] && !goals_[i]->shouldContinue(ctx)) {
            goals_[i]->stop();
            active_[i] = false;
        }
    }

    // Recompute the claimed flags from the still-active set.
    used_flags_ = 0;
    for (size_t i = 0; i < goals_.size(); ++i) {
        if (active_[i])
            used_flags_ |= goals_[i]->flags();
    }

    // Activate runnable goals in priority order, skipping any whose
    // flags overlap an already-claimed bit.
    for (size_t gi : order_) {
        if (active_[gi])
            continue;
        const uint8_t f = goals_[gi]->flags();
        if ((used_flags_ & f) != 0)
            continue;
        if (!goals_[gi]->canStart(ctx, *this))
            continue;
        goals_[gi]->start(ctx, *this);
        active_[gi] = true;
        used_flags_ |= f;
    }

    // Let every active goal write its intent. Overlapping flag claims
    // are impossible here, so goals never overwrite each other's field.
    for (size_t gi : order_) {
        if (active_[gi])
            goals_[gi]->tick(ctx, *this);
    }
}

float faceYaw(double dx, double dz) {
    // Protocol-84 convention, verified against Player::getDirectionVector
    // (x = -sin(yaw), z = cos(yaw)): a mob facing (dx, dz) gets
    // atan2(-dx, dz). atan2(+dx, dz) mirrors the facing across the Z axis
    // and would render the model walking backwards.
    return (float)(std::atan2(-dx, dz) * 180.0 / 3.14159265358979323846);
}

std::unique_ptr<MobBrain> makeMobBrain(uint8_t kind, uint64_t seed) {
    auto brain = std::make_unique<MobBrain>();
    brain->setKind(kind);
    mcp_random_seed(&brain->rng(), (uint32_t)(seed ^ (seed >> 32)));
    // Stage 1 covers the Passive family; Neutral and Hostile arrive
    // with their own stage (docs/MOB_AI.md section 8). Kinds without a
    // behavior stage keep the pre-AI static record: an empty brain is
    // never thought about, never moved, never sent packets.
    if (kind == (uint8_t)mobs::MobKind::Passive) {
        brain->addGoal(std::make_unique<IdleLookGoal>());
        brain->addGoal(std::make_unique<LookAtPlayerGoal>());
        brain->addGoal(std::make_unique<WanderGoal>());
    }
    return brain;
}

} // namespace cursestone::entity::ai
