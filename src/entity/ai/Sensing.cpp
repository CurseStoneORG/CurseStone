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
#include "entity/ai/Sensing.h"
#include "CurseStone.h"
#include "entity/ai/Mob.h"
#include "world/World.h"
#include <cmath>

namespace cursestone::entity::ai {

bool Sensing::canSee(int64_t targetEid, double tx, double ty, double tz) {
    if (!mob_ || !mob_->level())
        return false;
    const auto it = cache_.find(targetEid);
    if (it != cache_.end())
        return it->second;

    World& world = *mob_->level();
    const double sx = mob_->posX();
    const double sy = mob_->posY() + mob_->getHeadHeight();
    const double sz = mob_->posZ();
    const double dx = tx - sx;
    const double dy = ty - sy;
    const double dz = tz - sz;
    const double dist = std::sqrt(dx * dx + dy * dy + dz * dz);

    // Sample the ray every half block. The endpoint cell is skipped
    // (the target's own body cell must not block its own sight line).
    bool visible = true;
    const int steps = (int)(dist / 0.5);
    for (int i = 1; i < steps; ++i) {
        const double t = (double)i / (double)steps;
        const int bx = (int)std::floor(sx + dx * t);
        const int by = (int)std::floor(sy + dy * t);
        const int bz = (int)std::floor(sz + dz * t);
        const uint8_t id = world.getBlockId(bx, by, bz);
        if (id != 0 && World::entityCollidable(id)) {
            visible = false;
            break;
        }
    }
    cache_.emplace(targetEid, visible);
    return visible;
}

} // namespace cursestone::entity::ai
