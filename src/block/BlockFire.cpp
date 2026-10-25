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
// BlockFire.cpp — Fire block, simplified.
//
// DESIGN DECISION (v6):
//   The reference-accurate fire propagation system from v5 caused severe
//   tick-loop stalls (88s freeze in workbench222274838282bdjsj.zip).
//   Even with the anti-cascade dedup + cap, the fire-spread logic
//   itself does too much work per tick when there are multiple fires.
//
//   Fire must not spread or catch blocks for now; keep it display-only.
//
//   So this version is INTENTIONALLY minimal:
//     - scheduleUpdate() is a no-op. No tasks are ever queued.
//     - onScheduledUpdate() is a no-op. (Kept for ABI but never called.)
//     - tryToCatchBlockOnFire, getChanceOfNeighborsEncouragingFire,
//       canNeighborBurn, isTopFacingSurfaceSolid all retained as
//       static helpers because ItemFlintSteel::onActivate still uses
//       the last two for the "has_support" check.
//     - getBurnAbility / getBurnChance retained but unused by anything
//       other than the now-dead canNeighborBurn.
//
//   Result: a fire placed by flint-and-steel sits there forever as a
//   visual block. It doesn't age, doesn't spread, doesn't go out, and
//   critically DOES NOT schedule any tick task. Zero stall risk.
#include "BlockFire.h"
#include "BlockIds.h"
#include "world/World.h"
#include "world/format/Chunk.h"
#include "Server.h"
#include "scheduler/ServerScheduler.h"
#include "scheduler/Task.h"
#include "entity/Entity.h"
#include <cstdlib>
#include <algorithm>

namespace cursestone::block {

// ============================================================
// scheduleUpdate — NO-OP in v6.
// Previously this queued a TrackedFireUpdateTask with a 30-40 tick
// delay. Every fire that was placed would eventually wake up and
// schedule more fires, producing the cascade that froze the server.
// Now it does nothing — fires are static visual blocks.
// ============================================================
void BlockFire::scheduleUpdate(world::World* /*world*/, int32_t /*x*/, int32_t /*y*/,
                               int32_t /*z*/) {
    // Intentionally empty.
    // Fire does not tick. Fire does not spread. Fire does not age.
}

// ============================================================
// Tablas de flamabilidad — valores the reference server verbatim (sin uso en v6
// más allá de canNeighborBurn, que a su vez solo se usa desde
// ItemFlintSteel para el check "has_support"). Se mantienen para
// que el código compile sin cambios en los headers.
// ============================================================
int BlockFire::getBurnAbility(uint8_t id) {
    switch (id) {
    case PLANKS:
        return 20;
    case LOG:
        return 5;
    case LEAVES:
        return 60;
    case BOOKSHELF:
        return 30;
    case TNT:
        return 15;
    case WOOL:
        return 30;
    case TALL_GRASS:
        return 60;
    case DEAD_BUSH:
        return 60;
    case FENCE:
        return 20;
    case VINE:
        return 15;
    case WOOD_STAIRS:
        return 20;
    case 126:
        return 20;
    default:
        return 0;
    }
}

int BlockFire::getBurnChance(uint8_t id) {
    switch (id) {
    case PLANKS:
        return 5;
    case LOG:
        return 5;
    case LEAVES:
        return 30;
    case BOOKSHELF:
        return 30;
    case TNT:
        return 15;
    case WOOL:
        return 10;
    case TALL_GRASS:
        return 60;
    case DEAD_BUSH:
        return 60;
    case FENCE:
        return 5;
    case VINE:
        return 15;
    case WOOD_STAIRS:
        return 5;
    case 126:
        return 5;
    default:
        return 0;
    }
}

// ============================================================
// isTopFacingSurfaceSolid
// Used by ItemFlintSteel::onActivate to decide whether the fire
// has solid ground below it (and therefore should be placed).
// ============================================================
bool BlockFire::isTopFacingSurfaceSolid(world::World* world, int32_t x, int32_t y, int32_t z) {
    if (y < 0 || y >= 256)
        return false;
    if (!world->isChunkLoaded(x, z))
        return false;
    uint8_t bid = world->getBlockId(x, y, z);
    switch (bid) {
    case AIR:
    case FIRE:
    case TALL_GRASS:
    case DEAD_BUSH:
    case FLOWING_WATER:
    case WATER:
    case FLOWING_LAVA:
    case LAVA:
        return false;
    default:
        return true;
    }
}

// ============================================================
// canNeighborBurn
// Used by ItemFlintSteel::onActivate to decide whether to place
// fire even without solid ground (e.g. next to a plank wall).
// ============================================================
bool BlockFire::canNeighborBurn(world::World* world, int32_t x, int32_t y, int32_t z) {
    static const int dx[6] = {0, 0, 0, 0, -1, 1};
    static const int dy[6] = {-1, 1, 0, 0, 0, 0};
    static const int dz[6] = {0, 0, -1, 1, 0, 0};
    for (int i = 0; i < 6; i++) {
        int ny = y + dy[i];
        if (ny < 0 || ny >= 256)
            continue;
        int32_t nx = x + dx[i], nz = z + dz[i];
        if (!world->isChunkLoaded(nx, nz))
            continue;
        if (getBurnAbility(world->getBlockId(nx, ny, nz)) > 0)
            return true;
    }
    return false;
}

// ============================================================
// getChanceOfNeighborsEncouragingFire — DEAD in v6.
// Retained as a stub so the header signature stays valid. Never called.
// ============================================================
int BlockFire::getChanceOfNeighborsEncouragingFire(world::World* /*world*/, int32_t /*x*/,
                                                   int32_t /*y*/, int32_t /*z*/) {
    return 0;
}

// ============================================================
// tryToCatchBlockOnFire — DEAD in v6.
// Retained as a stub. Never called.
// ============================================================
void BlockFire::tryToCatchBlockOnFire(world::World* /*world*/, int32_t /*tx*/, int32_t /*ty*/,
                                      int32_t /*tz*/, int /*bound*/, int /*damage*/) {
    // No-op.
}

// ============================================================
// onEntityCollide — mirrors the reference server Fire::onEntityCollide.
// Players/mobs walking into a fire still take damage. This is the
// one piece of "fire logic" that runs OUTSIDE the scheduler (it's
// invoked by the entity-tick loop directly), so it doesn't contribute
// to the cascade.
// ============================================================
void BlockFire::onEntityCollide(world::World* /*world*/, int32_t /*bx*/, int32_t /*by*/,
                                int32_t /*bz*/, entity::Entity* entity) {
    if (!entity)
        return;
    // TODO: ignorar si entity tiene efecto FIRE_RESISTANCE
    entity->attack(1);
    entity->setOnFire(8);
}

// ============================================================
// onScheduledUpdate — NO-OP in v6.
// Should never be called because scheduleUpdate is a no-op, but kept
// as a defensive stub in case something else queues a fire task
// (e.g. legacy saved chunks referencing BLOCK_UPDATE_SCHEDULED).
// ============================================================
void BlockFire::onScheduledUpdate(world::World* /*world*/, int32_t /*x*/, int32_t /*y*/,
                                  int32_t /*z*/) {
    // Intentionally empty — fire is a static visual block in v6.
}

} // namespace cursestone::block
