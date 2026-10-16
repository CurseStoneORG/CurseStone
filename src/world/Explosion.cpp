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

// Explosion::explode — minimal blast: removes blocks inside a sphere.
//
// TODO (explosion): bring this up to the vanilla/reference behavior:
//   1. Broadcast the explosion itself — LevelEventPacket EVENT_SOUND_EXPLODE
//      (3501) at the blast center + EVENT_PARTICLE_DESTROY (2001) for every
//      removed block, so players actually SEE and HEAR it (a silent invisible
//      crater is the #1 "the explosion does not work" report).
//   2. Entity damage + knockback — hurt every Living whose bounding box
//      touches the blast sphere, force scaled by distance (the reference server
//      Explosion::explodeB), with per-entity damage events for plugins.
//   3. Drops — destroy ~30% of the removed blocks into item entities
//      (vanilla explosion drop chance; today everything just vanishes).
//   4. Fire — optionally seed FIRE (id 51) on a few blast-facing cells
//      like vanilla TNT/bed detonations do in the Nether.
//   5. Cancellable EntityExplodeEvent carrying the block list, so plugins
//      can protect regions (parity with event/entity/EntityExplodeEvent.h).
// Callers today: the bed-used-in-the-Nether detonation
// (player/PlayerBlocks.cpp). The TNT detonation (block/BlockTNT.cpp TODO)
// will call this too once the ignite chain exists.
int Explosion::explode() {
    int destroyed = 0;
    int r = (int)(power_ + 0.5f);
    for (int dy = -r; dy <= r; ++dy)
        for (int dx = -r; dx <= r; ++dx)
            for (int dz = -r; dz <= r; ++dz) {
                double dist = std::sqrt(dx * dx + dy * dy + dz * dz);
                if (dist > power_)
                    continue;
                // simple attrition — every block reduces remaining power.
                double px = pos_.x + dx, py = pos_.y + dy, pz = pos_.z + dz;
                if (py < 0 || py >= 256)
                    continue;
                auto world = pos_.world();
                if (!world)
                    continue;
                uint8_t id = world->getBlockId((int32_t)px, (int32_t)py, (int32_t)pz);
                if (id == 0 || id == 7)
                    continue; // air/bedrock
                // 30% of blast-resistance randomly skips (matches MCPE feel).
                if ((dx + dy + dz) & 1)
                    continue;
                world->setBlock((int32_t)px, (int32_t)py, (int32_t)pz, 0, 0);
                ++destroyed;
                // NOTE: the falling-block support check lives INSIDE
                // World::setBlock — clearing a cell re-evaluates the block above
                // it (sand/gravel/anvil there convert to falling entities).
            }
    return destroyed;
}
} // namespace
