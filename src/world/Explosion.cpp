/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

 * CurseStone | Open Source Server Software
 *
 * Copyright (C) 2026 CurseStoneTeam
 *
 * Developed by CurseStoneTeam.
 * Built for an open and extensible Minecraft server ecosystem.
 *
 * Licensed under the GNU Lesser General Public License v3.0 or later.
 * You may study, modify, and redistribute this software under its license.
 *
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 * Repository: https://github.com/CurseStoneTeam/CurseStone
 *
 */
#include "Explosion.h"
#include "world/format/Chunk.h"
#include "world/World.h"
#include <cmath>
namespace cursestone::world {
int Explosion::explode() {
    int destroyed = 0;
    int r = (int)(power_ + 0.5f);
    for (int dy = -r; dy <= r; ++dy)
    for (int dx = -r; dx <= r; ++dx)
    for (int dz = -r; dz <= r; ++dz) {
        double dist = std::sqrt(dx*dx + dy*dy + dz*dz);
        if (dist > power_) continue;
        // simple attrition — every block reduces remaining power.
        double px = pos_.x + dx, py = pos_.y + dy, pz = pos_.z + dz;
        if (py < 0 || py >= 256) continue;
        auto world = pos_.world();
        if (!world) continue;
        uint8_t id = world->getBlockId((int32_t)px, (int32_t)py, (int32_t)pz);
        if (id == 0 || id == 7) continue; // air/bedrock
        // 30% of blast-resistance randomly skips (matches MCPE feel).
        if ((dx + dy + dz) & 1) continue;
        world->setBlock((int32_t)px, (int32_t)py, (int32_t)pz, 0, 0);
        ++destroyed;
    }
    return destroyed;
}
} // namespace
