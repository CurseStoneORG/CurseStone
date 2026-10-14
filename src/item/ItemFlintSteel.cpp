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
// ItemFlintSteel — flint and steel: places fire and lights nether portals.
//
// onActivate() follows the two paths of PMMP 0.15 (Genisys/Infinity
// FlintSteel::onActivate), the reference real 0.15.10 clients work with:
//   - Obsidian target: measure the obsidian frame around the clicked
//     block (X axis first, then Z axis) and, if it is a valid portal
//     frame (4..23 wide, 5..23 tall, full top row), fill the interior
//     with portal blocks (id 90, meta = axis). Every filled cell is
//     broadcast to the viewers, otherwise the client only renders the
//     single cell the click landed on.
//   - Any other solid target: place a fire block (classic PMMP behavior;
//     also reached when the obsidian frame is incomplete, like PMMP).
// In survival each use costs 2 durability points.
#include "ItemFlintSteel.h"
#include "ItemStack.h"
#include "block/BlockFire.h"
#include "block/BlockIds.h"
#include "world/World.h"
#include "Player.h"
#include "inventory/PlayerInventory.h"
#include "network/protocol/UpdateBlockPacket.h"
#include "Server.h"
#include <algorithm>
#include <memory>
#include <tuple>
#include <utility>
#include <vector>

namespace cursestone::item {

using namespace cursestone::block;

/// @brief Outer size limits of a portal frame, per axis (PMMP 0.15:
///        4..23 wide, 5..23 tall, measured on the obsidian outline).
static constexpr int kPortalOuterMin = 4;
static constexpr int kPortalOuterMax = 23;

/// @brief Can this cell be part of a portal interior? Vanilla interiors
///        are air; accepting already-lit portal blocks makes relighting
///        a partially broken portal possible.
static bool isPortalInterior(world::World* w, int32_t x, int32_t y, int32_t z) {
    uint8_t id = w->getBlockId(x, y, z);
    return id == AIR || id == NETHER_PORTAL;
}

/// @brief Detects and lights a portal on the XY plane (opening running
///        along the X axis, Z fixed). (tx, ty, tz) is the clicked
///        obsidian block — the detection measures the obsidian frame,
///        never the clicked air cell, so lighting works no matter which
///        side of the frame the player clicks. On success every interior
///        cell is filled and appended to `filled` (x, y) for the client
///        update. Ported from PMMP 0.15 (Genisys/Infinity).
static bool tryPortalPlaneX(world::World* w, int32_t tx, int32_t ty, int32_t tz,
                            std::vector<std::tuple<int32_t, int32_t, int32_t>>& filled) {
    // Obsidian run of the clicked row along X.
    int32_t xMax = tx;
    while (xMax - tx < kPortalOuterMax &&
           w->getBlockId(xMax + 1, ty, tz) == OBSIDIAN) ++xMax;
    int32_t xMin = tx;
    while (tx - xMin < kPortalOuterMax &&
           w->getBlockId(xMin - 1, ty, tz) == OBSIDIAN) --xMin;
    int32_t countX = xMax - xMin + 1;
    if (countX < kPortalOuterMin || countX > kPortalOuterMax) return false;

    // First air row above the clicked one at both ends of the run; the
    // top frame row is the lower of the two.
    int32_t topL = ty, topR = ty;
    while (topL - ty <= kPortalOuterMax &&
           w->getBlockId(xMin, topL + 1, tz) == OBSIDIAN) ++topL;
    while (topR - ty <= kPortalOuterMax &&
           w->getBlockId(xMax, topR + 1, tz) == OBSIDIAN) ++topR;
    int32_t yMax = std::min(topL, topR) - 1;
    int32_t countY = yMax - ty + 2;   // outer height: base row + top row
    if (countY < kPortalOuterMin + 1 || countY > kPortalOuterMax) return false;

    // The top row must be a continuous obsidian run of the same width.
    int32_t countTop = 0;
    for (int32_t xx = xMin;
         xx <= xMax && w->getBlockId(xx, yMax, tz) == OBSIDIAN; ++xx) {
        ++countTop;
    }
    if (countTop != countX) return false;

    // Fill the interior with portal blocks (meta 0 = X axis).
    for (int32_t xx = xMin + 1; xx < xMax; ++xx) {
        for (int32_t yy = ty + 1; yy < yMax; ++yy) {
            if (!isPortalInterior(w, xx, yy, tz)) continue;
            w->setBlock(xx, yy, tz, NETHER_PORTAL, 0);
            filled.emplace_back(xx, yy, tz);
        }
    }
    return true;
}

/// @brief Same detection for the ZY plane (opening along the Z axis,
///        X fixed).
static bool tryPortalPlaneZ(world::World* w, int32_t tx, int32_t ty, int32_t tz,
                            std::vector<std::tuple<int32_t, int32_t, int32_t>>& filled) {
    int32_t zMax = tz;
    while (zMax - tz < kPortalOuterMax &&
           w->getBlockId(tx, ty, zMax + 1) == OBSIDIAN) ++zMax;
    int32_t zMin = tz;
    while (tz - zMin < kPortalOuterMax &&
           w->getBlockId(tx, ty, zMin - 1) == OBSIDIAN) --zMin;
    int32_t countZ = zMax - zMin + 1;
    if (countZ < kPortalOuterMin || countZ > kPortalOuterMax) return false;

    int32_t topL = ty, topR = ty;
    while (topL - ty <= kPortalOuterMax &&
           w->getBlockId(tx, topL + 1, zMin) == OBSIDIAN) ++topL;
    while (topR - ty <= kPortalOuterMax &&
           w->getBlockId(tx, topR + 1, zMax) == OBSIDIAN) ++topR;
    int32_t yMax = std::min(topL, topR) - 1;
    int32_t countY = yMax - ty + 2;
    if (countY < kPortalOuterMin + 1 || countY > kPortalOuterMax) return false;

    int32_t countTop = 0;
    for (int32_t zz = zMin;
         zz <= zMax && w->getBlockId(tx, yMax, zz) == OBSIDIAN; ++zz) {
        ++countTop;
    }
    if (countTop != countZ) return false;

    // Fill the interior with portal blocks (meta 1 = Z axis).
    for (int32_t zz = zMin + 1; zz < zMax; ++zz) {
        for (int32_t yy = ty + 1; yy < yMax; ++yy) {
            if (!isPortalInterior(w, tx, yy, zz)) continue;
            w->setBlock(tx, yy, zz, NETHER_PORTAL, 1);
            filled.emplace_back(tx, yy, zz);
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

    // Portal: the click landed on obsidian. The detection is anchored on
    // the TARGET obsidian (PMMP 0.15 semantics), X plane first, then Z
    // plane; every filled cell is broadcast so the client renders the
    // whole portal, not just the cell next to the click.
    if (targetId == OBSIDIAN) {
        std::vector<std::tuple<int32_t, int32_t, int32_t>> filled;
        if (tryPortalPlaneX(world, tx, ty, tz, filled) ||
            tryPortalPlaneZ(world, tx, ty, tz, filled)) {
            for (auto& [fx, fy, fz] : filled) {
                broadcastBlockUpdate(player, world, fx, fy, fz,
                                     NETHER_PORTAL, 0);
            }
            applyDurability(player);
            return true;
        }
        // Incomplete frame: fall through and place fire, like PMMP does.
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
