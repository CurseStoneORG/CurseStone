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
// Goal — one composable behavior of a mob brain.
//
// Port of <minecraftpe/impl/entity/ai/Goal.cpp> (MCPE 0.8.1 decompile,
// oldminecraftcommunity). The class shape is the era one: a goal carries
// the control flags it claims (move / look / jump), the GoalSelector
// decides when it runs, and the goal drives the mob through the controls
// (LookControl::setLookAt, PathNavigation::moveTo, JumpControl::jump).
//
// Adaptation: the era methods take no arguments because every goal held
// its holder pointer from the constructor. Here the Mob facade the brain
// owns is passed through the selector instead, so goal objects never
// store a pointer that can outlive the bound record.
#pragma once
#include <cstdint>

namespace cursestone::entity::ai {

class Mob;

/// @brief Control-flag bits a goal claims while it runs (the era
///        setRequiredControlFlags values).
///        1 = MOVE, 2 = LOOK, 4 = JUMP/swim. The GoalSelector refuses to
///        start a goal whose flags overlap an already-running goal's
///        flags, which is the era mutual-exclusion contract.
enum GoalControlFlags : int32_t {
    kControlNone = 0,
    kControlMove = 1,
    kControlLook = 2,
    kControlJump = 4,
};

class Goal {
  public:
    Goal() = default;
    virtual ~Goal() = default;

    Goal(const Goal&) = delete;
    Goal& operator=(const Goal&) = delete;

    /// @brief Whether the goal wants to start this evaluation.
    virtual bool canUse(Mob& mob) = 0;

    /// @brief Whether a running goal keeps running. The era default
    ///        delegates to canUse().
    virtual bool canContinueToUse(Mob& mob) { return canUse(mob); }

    /// @brief Whether an already-running goal can be interrupted by a
    ///        higher-priority goal that wants the same flags. The era
    ///        default allows interruption.
    virtual bool canInterrupt() { return true; }

    virtual void start(Mob& mob) { (void)mob; }
    virtual void stop(Mob& mob) { (void)mob; }

    /// @brief Runs on every selector pass while the goal is running.
    ///        The era default was an empty body (Goal.cpp), so goals
    ///        without per-tick work (the stroll family) simply omit it.
    virtual void tick(Mob& mob) { (void)mob; }

    /// @brief Replaces the era constructor-time setRequiredControlFlags.
    void setRequiredControlFlags(int32_t flags) { control_flags_ = flags; }

    int32_t getRequiredControlFlags() const { return control_flags_; }

  protected:
    int32_t control_flags_ = kControlNone;
};

} // namespace cursestone::entity::ai
