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
// Sensing — the can-see test the combat goals use.
//
// The MCPE 0.8.1 decompile ships <minecraftpe/impl/entity/ai/Sensing.cpp>
// as an empty body (its era fields _canSee/_cannotSee survive in the
// Mob constructor). The behavior the call sites require is the classic
// era one: a raycast from the mob's eye to the target's eye line,
// blocked by solid blocks, cached per tick per target (the era class
// clears both caches at the top of every newServerAiStep).
#pragma once
#include <cstdint>
#include <unordered_map>

namespace cursestone::entity::ai {

class Mob;

class Sensing {
  public:
    explicit Sensing(Mob* mob) : mob_(mob) {}

    /// @brief True when @p target is visible from the mob's eye. @p tx /
    ///        @p ty / @p tz is the point the ray aims at (the caller
    ///        picks the eye line). Results cache per (eid) for one tick.
    bool canSee(int64_t targetEid, double tx, double ty, double tz);

    /// @brief Era newServerAiStep clears the two caches every tick.
    void clearCache() {
        cache_.clear();
    }

  private:
    Mob* mob_;
    std::unordered_map<int64_t, bool> cache_;
};

} // namespace cursestone::entity::ai
