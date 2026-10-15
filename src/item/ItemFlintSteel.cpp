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
// onActivate() ports the frame measurement of PMMP 0.15 (Infinity
// FlintSteel::onActivate), the reference real 0.15.10 clients work with:
//   - Measure the obsidian runs anchored on the clicked obsidian block,
//     X axis first, then Z axis. The side-column walk STARTS ON the base
//     row and counts every obsidian cell, so it ends one block ABOVE the
//     highest obsidian; "- 1" then lands exactly on the top frame row
//     (4..23 wide, 5..23 tall). The top row must be a continuous
//     obsidian run as wide as the base. On success the interior is
//     filled with portal blocks (id 90, meta = axis: 0 X / 1 Z) and
//     every filled cell is broadcast to the viewers, otherwise the
//     client only renders the single cell the click landed on.
//   - Extra over Infinity: if the click lands on a SIDE column (mid
//     height), the frame measurement from the clicked row cannot work
//     (the row run is only one block), so the detection walks DOWN the
//     column to the lowest obsidian and retries. This makes lighting
//     work from any frame block, closer to vanilla.
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

/// @brief A portal cell that was filled by the detection: position plus
///        the axis metadata the client must render it with.
using FilledCell = std::tuple<int32_t, int32_t, int32_t, uint8_t>;

/// @brief Can this cell be part of a portal interior? Vanilla interiors
///        are air; accepting already-lit portal blocks makes relighting
///        a partially broken portal possible.
static bool isPortalInterior(world::World* w, int32_t x, int32_t y, int32_t z) {
    uint8_t id = w->getBlockId(x, y, z);
    return id == AIR || id == NETHER_PORTAL;
}

/// @brief Detects and lights a portal on the XY plane (opening running
///        along the X axis, Z fixed). (tx, ty, tz) is the clicked
///        obsidian block. The frame measurement follows Infinity/PMMP:
///        the side-column walk starts ON the base row and counts every
///        obsidian cell (so it ends one block ABOVE the highest
///        obsidian), and "- 1" lands on the top frame row. On success
///        every interior cell is filled and appended to `filled` with
///        its axis metadata. Returns false BEFORE touching any block
///        when the frame is invalid, so callers can safely retry.
static bool tryPortalPlaneX(world::World* w, int32_t tx, int32_t ty, int32_t tz,
                            std::vector<FilledCell>& filled) {
    // Obsidian run of the clicked row along X (the base row candidate).
    int32_t xMax = tx;
    while (xMax - tx < kPortalOuterMax && w->getBlockId(xMax + 1, ty, tz) == OBSIDIAN)
        ++xMax;
    int32_t xMin = tx;
    while (tx - xMin < kPortalOuterMax && w->getBlockId(xMin - 1, ty, tz) == OBSIDIAN)
        --xMin;
    int32_t countX = xMax - xMin + 1;
    if (countX < kPortalOuterMin || countX > kPortalOuterMax)
        return false;

    // Side-column heights, counted from the base row (base included) —
    // exactly how the Infinity/PMMP for-loop counts them, so topL/topR
    // end one block ABOVE the highest obsidian and yMax lands on the top
    // frame row. Using the topmost obsidian directly and then subtracting
    // one (the previous port) skipped the top row and made the top-row
    // check below always fail: the portal never lit.
    int32_t topL = ty, topR = ty;
    for (int32_t y = ty; y - ty <= kPortalOuterMax && w->getBlockId(xMin, y, tz) == OBSIDIAN; ++y) {
        topL = y + 1;
    }
    for (int32_t y = ty; y - ty <= kPortalOuterMax && w->getBlockId(xMax, y, tz) == OBSIDIAN; ++y) {
        topR = y + 1;
    }
    int32_t yMax = std::min(topL, topR) - 1;
    int32_t countY = yMax - ty + 2; // outer height: base row + top row
    if (countY < kPortalOuterMin + 1 || countY > kPortalOuterMax)
        return false;

    // The top row must be a continuous obsidian run of the same width.
    int32_t countTop = 0;
    for (int32_t xx = xMin; xx <= xMax && w->getBlockId(xx, yMax, tz) == OBSIDIAN; ++xx) {
        ++countTop;
    }
    if (countTop != countX)
        return false;

    // Fill the interior with portal blocks (meta 0 = X axis).
    for (int32_t xx = xMin + 1; xx < xMax; ++xx) {
        for (int32_t yy = ty + 1; yy < yMax; ++yy) {
            if (!isPortalInterior(w, xx, yy, tz))
                continue;
            w->setBlock(xx, yy, tz, NETHER_PORTAL, 0);
            filled.emplace_back(xx, yy, tz, 0);
        }
    }
    return true;
}

/// @brief Same detection for the ZY plane (opening along the Z axis,
///        X fixed). Meta 1 = Z axis.
static bool tryPortalPlaneZ(world::World* w, int32_t tx, int32_t ty, int32_t tz,
                            std::vector<FilledCell>& filled) {
    int32_t zMax = tz;
    while (zMax - tz < kPortalOuterMax && w->getBlockId(tx, ty, zMax + 1) == OBSIDIAN)
        ++zMax;
    int32_t zMin = tz;
    while (tz - zMin < kPortalOuterMax && w->getBlockId(tx, ty, zMin - 1) == OBSIDIAN)
        --zMin;
    int32_t countZ = zMax - zMin + 1;
    if (countZ < kPortalOuterMin || countZ > kPortalOuterMax)
        return false;

    // Side-column heights counted from the base row (see tryPortalPlaneX).
    int32_t topL = ty, topR = ty;
    for (int32_t y = ty; y - ty <= kPortalOuterMax && w->getBlockId(tx, y, zMin) == OBSIDIAN; ++y) {
        topL = y + 1;
    }
    for (int32_t y = ty; y - ty <= kPortalOuterMax && w->getBlockId(tx, y, zMax) == OBSIDIAN; ++y) {
        topR = y + 1;
    }
    int32_t yMax = std::min(topL, topR) - 1;
    int32_t countY = yMax - ty + 2;
    if (countY < kPortalOuterMin + 1 || countY > kPortalOuterMax)
        return false;

    int32_t countTop = 0;
    for (int32_t zz = zMin; zz <= zMax && w->getBlockId(tx, yMax, zz) == OBSIDIAN; ++zz) {
        ++countTop;
    }
    if (countTop != countZ)
        return false;

    // Fill the interior with portal blocks (meta 1 = Z axis).
    for (int32_t zz = zMin + 1; zz < zMax; ++zz) {
        for (int32_t yy = ty + 1; yy < yMax; ++yy) {
            if (!isPortalInterior(w, tx, yy, zz))
                continue;
            w->setBlock(tx, yy, zz, NETHER_PORTAL, 1);
            filled.emplace_back(tx, yy, zz, 1);
        }
    }
    return true;
}

/// @brief Broadcasts a block change to every player of the same world
///        that can see that chunk.
static void broadcastBlockUpdate(Player* player, world::World* world, int32_t x, int32_t y,
                                 int32_t z, uint8_t id, uint8_t data) {
    if (!player || !player->server())
        return;
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

/// @brief Registers a lit portal in its world's persistent portal
///        registry (anchor = first interior cell) so cross-dimension
///        travel can find it later.
static void registerLitPortal(world::World* world, const std::vector<FilledCell>& filled) {
    if (!world || filled.empty())
        return;
    const FilledCell& anchor = filled.front();
    world->portals().add(std::get<0>(anchor), std::get<1>(anchor), std::get<2>(anchor),
                         std::get<3>(anchor));
    world->savePortals();
}

/// @brief Broadcasts every portal cell the detection filled, each one
///        with its own axis metadata (0 = X plane, 1 = Z plane). Sending
///        a fixed meta here made Z-plane portals render with the wrong
///        orientation on the client.
static void broadcastFilledCells(Player* player, world::World* world,
                                 const std::vector<FilledCell>& filled) {
    for (const auto& [fx, fy, fz, fmeta] : filled) {
        broadcastBlockUpdate(player, world, fx, fy, fz, NETHER_PORTAL, fmeta);
    }
}

void ItemFlintSteel::applyDurability(Player* player) {
    if (!player || !player->isSurvival())
        return;

    auto inv = player->getInventory();
    if (!inv)
        return;

    auto held = inv->getHeldItem();
    if (held.isEmpty() || held.id != 259)
        return;

    held.meta += 2;
    if (held.meta > MAX_DURABILITY) {
        inv->clear(inv->heldSlot());
    } else {
        inv->setItemInHand(held);
    }
    // Both clear() and setItemInHand() already fire
    // PlayerInventory::onSlotChange(), which sends the
    // ContainerSetSlotPacket confirmations on its own.
}

bool ItemFlintSteel::onActivate(world::World* world, Player* player, int32_t bx, int32_t by,
                                int32_t bz, int32_t tx, int32_t ty, int32_t tz, uint8_t /*face*/) {
    if (!world || !player)
        return false;
    if (!player->server() || !player->server()->netherEnabled())
        return false;

    uint8_t targetId = world->getBlockId(tx, ty, tz);
    uint8_t airId = world->getBlockId(bx, by, bz);

    // Portal: the click landed on obsidian. The detection is anchored on
    // the TARGET obsidian (PMMP 0.15 semantics), X plane first, then Z
    // plane; every filled cell is broadcast so the client renders the
    // whole portal, not just the cell next to the click.
    if (targetId == OBSIDIAN) {
        std::vector<FilledCell> filled;

        // 1) Infinity-faithful pass: measure the frame using the clicked
        //    row as the base row. This lights the portal when the player
        //    clicks the bottom row of the frame.
        if (tryPortalPlaneX(world, tx, ty, tz, filled) ||
            tryPortalPlaneZ(world, tx, ty, tz, filled)) {
            broadcastFilledCells(player, world, filled);
            registerLitPortal(world, filled);
            applyDurability(player);
            return true;
        }

        // 2) Side-column clicks: on a mid-height frame block the row run
        //    is a single block, so pass 1 can never measure a frame from
        //    it. Walk DOWN this obsidian column to its lowest cell (the
        //    real base row) and retry both planes — lighting then works
        //    from any frame block, closer to vanilla. The plane helpers
        //    validate fully before touching the world, so retrying is
        //    side-effect free.
        int32_t by = ty;
        int steps = 0;
        while (by > 0 && steps < kPortalOuterMax && world->getBlockId(tx, by - 1, tz) == OBSIDIAN) {
            --by;
            ++steps;
        }
        if (by != ty && (tryPortalPlaneX(world, tx, by, tz, filled) ||
                         tryPortalPlaneZ(world, tx, by, tz, filled))) {
            broadcastFilledCells(player, world, filled);
            registerLitPortal(world, filled);
            applyDurability(player);
            return true;
        }

        // Incomplete frame: fall through and place fire, like PMMP does.
    }

    // Fire: the placement cell must be air and the clicked target must
    // be a non-air, non-liquid block.
    if (airId != AIR)
        return false;
    if (targetId == AIR)
        return false;
    if (targetId == FLOWING_WATER || targetId == WATER)
        return false;
    if (targetId == FLOWING_LAVA || targetId == LAVA)
        return false;

    world->setBlock(bx, by, bz, FIRE, 0);
    broadcastBlockUpdate(player, world, bx, by, bz, FIRE, 0);

    applyDurability(player);
    return true;
}

} // namespace cursestone::item
