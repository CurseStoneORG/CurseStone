/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

 * CurseStone | Open Source server Software
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
// ItemFlintSteel — encendedor: places fire and enciende portales of the Nether.
//
// onActivate() accepts two paths according to the block target:
//   - obsidian: looks for the rectangle interior of the portal (axis X and luego
//     axis Z) and, if the frame is complete, it fills with blocks of
//     portal (id 90, meta = axis).
//   - any other solid: places a block of fire (behavior
//     clásico of PMMP FlintSteel::onActivate).
// in survival each use wears down 2 points of durability.
#include "ItemFlintSteel.h"
#include "ItemStack.h"
#include "block/BlockFire.h"
#include "block/BlockIds.h"
#include "world/World.h"
#include "Player.h"
#include "inventory/PlayerInventory.h"
#include "network/protocol/UpdateBlockPacket.h"
#include "Server.h"
#include <memory>

namespace cursestone::item {

using namespace cursestone::block;

/// @brief limit vanilla of the interior of the portal in each axis.
static constexpr int kPortalInteriorMax = 21;

/// @brief ¿the block can be part part of the interior of the portal?
///        the interior vanilla is air; accept also portal already
///        lit allows relight a portal partially broken.
static bool isPortalInterior(world::World* w, int32_t x, int32_t y, int32_t z) {
    uint8_t id = w->getBlockId(x, y, z);
    return id == AIR || id == NETHER_PORTAL;
}

/// @brief checks and fills a portal in the plane XY (portal to it along
///        of the axis X, with Z fixed). (ix, iy) is a cell CANDIDATA of the
///        interior (not necesariamente the esquina).
static bool tryPortalPlaneX(world::World* w, int32_t ix, int32_t iy, int32_t z) {
    // Extender the interior to the left and to the right.
    int32_t xMin = ix;
    while (ix - xMin < kPortalInteriorMax &&
           isPortalInterior(w, xMin - 1, iy, z)) {
        --xMin;
    }
    int32_t xMax = ix;
    while (xMax - ix < kPortalInteriorMax &&
           isPortalInterior(w, xMax + 1, iy, z)) {
        ++xMax;
    }
    int32_t width = xMax - xMin + 1;
    if (width < 2 || width > kPortalInteriorMax) return false;

    // Extender hacia above from the fila base.
    int32_t yTop = iy;
    while (yTop - iy < kPortalInteriorMax) {
        bool rowAir = true;
        for (int32_t xx = xMin; xx <= xMax; ++xx) {
            if (!isPortalInterior(w, xx, yTop + 1, z)) { rowAir = false; break; }
        }
        if (!rowAir) break;
        ++yTop;
    }
    int32_t height = yTop - iy + 1;
    if (height < 3 || height > kPortalInteriorMax) return false;

    // frame: base, techo and laterales of obsidian around of the interior.
    for (int32_t xx = xMin; xx <= xMax; ++xx) {
        if (w->getBlockId(xx, iy - 1, z) != OBSIDIAN) return false;
        if (w->getBlockId(xx, yTop + 1, z) != OBSIDIAN) return false;
    }
    for (int32_t yy = iy; yy <= yTop; ++yy) {
        if (w->getBlockId(xMin - 1, yy, z) != OBSIDIAN) return false;
        if (w->getBlockId(xMax + 1, yy, z) != OBSIDIAN) return false;
    }

    // fill with portal (meta 0 = axis X).
    for (int32_t xx = xMin; xx <= xMax; ++xx) {
        for (int32_t yy = iy; yy <= yTop; ++yy) {
            w->setBlock(xx, yy, z, NETHER_PORTAL, 0);
        }
    }
    return true;
}

/// @brief Ídem tryPortalPlaneX for the plane ZY (portal to it along of the
///        axis Z, with X fixed).
static bool tryPortalPlaneZ(world::World* w, int32_t x, int32_t iy, int32_t iz) {
    int32_t zMin = iz;
    while (iz - zMin < kPortalInteriorMax &&
           isPortalInterior(w, x, iy, zMin - 1)) {
        --zMin;
    }
    int32_t zMax = iz;
    while (zMax - iz < kPortalInteriorMax &&
           isPortalInterior(w, x, iy, zMax + 1)) {
        ++zMax;
    }
    int32_t width = zMax - zMin + 1;
    if (width < 2 || width > kPortalInteriorMax) return false;

    int32_t yTop = iy;
    while (yTop - iy < kPortalInteriorMax) {
        bool rowAir = true;
        for (int32_t zz = zMin; zz <= zMax; ++zz) {
            if (!isPortalInterior(w, x, yTop + 1, zz)) { rowAir = false; break; }
        }
        if (!rowAir) break;
        ++yTop;
    }
    int32_t height = yTop - iy + 1;
    if (height < 3 || height > kPortalInteriorMax) return false;

    for (int32_t zz = zMin; zz <= zMax; ++zz) {
        if (w->getBlockId(x, iy - 1, zz) != OBSIDIAN) return false;
        if (w->getBlockId(x, yTop + 1, zz) != OBSIDIAN) return false;
    }
    for (int32_t yy = iy; yy <= yTop; ++yy) {
        if (w->getBlockId(x, yy, zMin - 1) != OBSIDIAN) return false;
        if (w->getBlockId(x, yy, zMax + 1) != OBSIDIAN) return false;
    }

    for (int32_t zz = zMin; zz <= zMax; ++zz) {
        for (int32_t yy = iy; yy <= yTop; ++yy) {
            w->setBlock(x, yy, zz, NETHER_PORTAL, 1);
        }
    }
    return true;
}

/// @brief broadcasts the change of block to the players of the same world
///        that puedan ver that chunk.
static void broadcastBlockUpdate(Player* player, world::World* world,
                                 int32_t x, int32_t y, int32_t z,
                                 uint8_t id, uint8_t data) {
    if (!player || !player->server()) return;
    auto ubp = std::make_shared<network::protocol::UpdateBlockPacket>();
    ubp->x = x;
    ubp->z = z;
    ubp->y = (uint8_t)y;
    ubp->blockId = id;
    ubp->blockData = data;
    ubp->flags = 0xFF;

    int32_t cx = x >> 4, cz = z >> 4;
    for (auto& other : player->server()->playersSnapshot()) {
        if (other && other->getWorld() == world && other->canSeeBlock(cx, cz))
            other->sendPacket(ubp);
    }
}

void ItemFlintSteel::applyDurability(Player* player) {
    if (!player || !player->isSurvival()) return;

    auto inv = player->getInventory();
    if (!inv) return;

    auto held = inv->getHeldItem();
    if (held.isEmpty() || held.id != 259) return;

    held.meta += 2;
    if (held.meta > MAX_DURABILITY) {
        inv->clear(inv->heldSlot());
    } else {
        inv->setItemInHand(held);
    }
    // so much clear() as setItemInHand() already disparan
    // PlayerInventory::onSlotChange(), that sends the confirmación
    // ContainerSetSlotPacket by its cuenta.
}

bool ItemFlintSteel::onActivate(world::World* world, Player* player,
                                int32_t bx, int32_t by, int32_t bz,
                                int32_t tx, int32_t ty, int32_t tz,
                                uint8_t /*face*/) {
    if (!world || !player) return false;
    if (!player->server() || !player->server()->netherEnabled()) return false;

    uint8_t targetId = world->getBlockId(tx, ty, tz);
    uint8_t airId    = world->getBlockId(bx, by, bz);

    // portal: the click cayó about obsidian. before place fire is
    // intenta ignite the portal with the cell of air clicked as
    // candidato of the interior (axis X first, luego axis Z).
    if (targetId == OBSIDIAN && airId == AIR) {
        if (tryPortalPlaneX(world, bx, by, bz) ||
            tryPortalPlaneZ(world, bx, by, bz)) {
            broadcastBlockUpdate(player, world, bx, by, bz,
                                 NETHER_PORTAL, 0);
            applyDurability(player);
            return true;
        }
        return false;   // obsidian without frame complete: not there is fire
    }

    // fire: only if the target is AIR and the target is a solid
    // (not air ni líquidos).
    if (airId != AIR) return false;
    if (targetId == AIR) return false;
    if (targetId == FLOWING_WATER || targetId == WATER) return false;
    if (targetId == FLOWING_LAVA  || targetId == LAVA)  return false;

    world->setBlock(bx, by, bz, FIRE, 0);
    broadcastBlockUpdate(player, world, bx, by, bz, FIRE, 0);

    applyDurability(player);
    return true;
}

} // namespace cursestone::item
