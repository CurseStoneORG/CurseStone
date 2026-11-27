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
// JumpControl — the jump request latch.
//
// Port of <minecraftpe/impl/entity/ai/JumpControl.cpp> (MCPE 0.8.1
// decompile): goals call jump(), tick() hands the flag to the mob and
// clears it. The actuation step consumes the mob's jumping flag (ground
// hop, water paddle).
#pragma once
#include "entity/ai/Mob.h"

namespace cursestone::entity::ai {

class Mob;

class JumpControl {
  public:
    explicit JumpControl(Mob* mob) : mob_(mob) {}

    void jump() { is_jumping_ = true; }

    /// @brief The pending flag (tests and diagnostics).
    bool isJumping() const { return is_jumping_; }

    void tick() {
        if (mob_)
            mob_->setJumping(is_jumping_);
        is_jumping_ = false;
    }

  private:
    Mob* mob_;
    bool is_jumping_ = false;
};

} // namespace cursestone::entity::ai
