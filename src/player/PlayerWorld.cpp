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
// PlayerWorld — world switching and Nether portal travel.
//
//   switchWorld()          moves the player to another world (sends
//                          ChangeDimensionPacket when the dimension changes)
//   finishDimensionSwitch() closes the cross-dimension switch once the new
//                          chunks are on the wire (PlayStatus + position)
//   checkPortal()          detects the player standing inside a portal
//                          block and starts the travel
//   teleportToNether()     Overworld -> Nether (coordinates scaled 1:8)
//   teleportToOverworld()  Nether -> Overworld (coordinates scaled 8:1)
//
// On MCPE 0.15.10 the client shows its loading screen when it receives
// ChangeDimensionPacket and closes it only when the new chunks AND
// PlayStatus(PLAYER_SPAWN) arrive — in that order. This file replicates
// the sequence the reference server/Genisys 0.15 used: SetTime, ChangeDimension,
// weather, then the chunk stream, then PlayStatus(PLAYER_SPAWN) and the
// position reset once the whole view has been sent.
#include "Player.h"
#include "Server.h"
#include "world/World.h"
#include "world/format/Chunk.h"
#include "player/PlayerInternal.h"
#include "network/protocol/ChangeDimensionPacket.h"
#include "network/protocol/MovePlayerPacket.h"
#include "network/protocol/PlayStatusPacket.h"
#include "network/protocol/SetTimePacket.h"
#include "event/EventManager.h"
#include "event/player/PlayerTeleportEvent.h"
#include "event/entity/EntityPortalEnterEvent.h"
#include "block/BlockIds.h"
#include "utils/MainLogger.hpp"
#include "utils/Utils.h"
#include <cmath>
#include <memory>

namespace cursestone {

World* Player::getWorld() const {
    if (world_)
        return world_;
    return srv_ ? srv_->getDefaultWorld() : nullptr;
}

// ---------------------------------------------------------------------
// switchWorld — the actual world change.
// ---------------------------------------------------------------------
// Same contract as teleport() (cancelable event + chunk re-stream) plus
// the dimensional part: if the target world lives in another dimension,
// ChangeDimensionPacket goes out BEFORE the chunks and
// PlayStatus(PLAYER_SPAWN) is deferred (see finishDimensionSwitch())
// until the whole new view has been streamed — sending it earlier
// leaves the 0.15.10 client frozen on the loading screen.
bool Player::switchWorld(World* target, double x, double y, double z, bool throughPortal) {
    (void)throughPortal;
    if (!srv_ || !target)
        return false;
    World* from = getWorld();

    {
        auto ev = event::EventManager::get().fire<event::player::PlayerTeleportEvent>(
            this, pos_.x, pos_.y, pos_.z, x, y, z);
        if (ev.isCancelled())
            return false;
        x = ev.toX;
        y = ev.toY;
        z = ev.toZ;
    }

    // The open container belongs to the previous world.
    closeOpenChest();

    // Sleeping belongs to the previous world too (the reference server Player
    // calls stopSleep() inside teleport()): a player dragged out of their
    // bed by a portal/plugin teleport stops sleeping, cancels their
    // skip-the-night vote and drops the sleeping pose for the viewers.
    stopSleep();

    bool dimension_change = !from || target->dimension() != from->dimension();

    world_ = target;
    pos_.x = x;
    pos_.y = y;
    pos_.z = z;
    fall_distance_ = 0.0f;
    last_pos_set_ = false;

    // teleport()-style re-stream: the client rebuilds its view around the
    // destination with the chunks of the new world.
    {
        std::lock_guard<std::mutex> lk(chunks_mtx_);
        used_chunks_.clear();
    }
    int cx = (int)std::floor(x) >> 4;
    int cz = (int)std::floor(z) >> 4;
    {
        std::lock_guard<std::mutex> lk(queue_mtx_);
        chunk_queue_.clear();
        chunk_queue_.emplace_back(cx, cz);
    }

    if (!spawned_.load(std::memory_order_relaxed)) {
        return true; // pre-spawn: doFirstSpawn() sends the initial packets
    }

    if (dimension_change) {
        // The reference server 0.15 switchLevel() order: SetTime first, then
        // ChangeDimensionPacket (dimension as one BYTE — an Int32 here
        // shifts the whole body and the client drops the packet, which
        // kept the overworld sky/fog over nether terrain), then the
        // per-world weather. PlayStatus(PLAYER_SPAWN) and the position
        // reset wait for the chunk stream (dimension_spawn_pending_).
        auto st = std::make_shared<network::protocol::SetTimePacket>();
        st->time = target->time();
        sendPacket(st);

        auto cd = std::make_shared<network::protocol::ChangeDimensionPacket>();
        cd->dimension = target->dimension();
        cd->x = (float)x;
        cd->y = (float)y;
        cd->z = (float)z;
        sendPacket(cd);

        target->sendWeatherTo(this);

        dimension_spawn_pending_ = true;
        dimension_switch_ms_ = mcp_utils_time_ms();
    } else {
        // Same dimension, different world: no loading screen involved,
        // sync the position right away.
        auto mp = std::make_shared<network::protocol::MovePlayerPacket>();
        mp->eid = runtimeId();
        mp->x = (float)x;
        mp->y = (float)(y + kEyeHeight); // on the wire Y is the eye height
        mp->z = (float)z;
        mp->yaw = yaw_;
        mp->bodyYaw = yaw_;
        mp->pitch = pitch_;
        mp->mode = network::protocol::MovePlayerPacket::MODE_RESET;
        mp->onGround = onGround();
        sendPacket(mp);
    }
    return true;
}

// ---------------------------------------------------------------------
// finishDimensionSwitch — closes a cross-dimension switch.
// ---------------------------------------------------------------------
// The 0.15.10 client leaves its dimension loading screen only when
// PlayStatus(PLAYER_SPAWN) arrives AFTER the new chunks; the position
// reset right after it gives the client its final coordinates, exactly
// like the reference server's checkTeleportPosition() flow.
void Player::finishDimensionSwitch() {
    dimension_spawn_pending_ = false;

    auto ps = std::make_shared<network::protocol::PlayStatusPacket>();
    ps->status = network::protocol::PlayStatusPacket::PLAYER_SPAWN;
    sendPacket(ps);

    auto mp = std::make_shared<network::protocol::MovePlayerPacket>();
    mp->eid = runtimeId();
    mp->x = (float)pos_.x;
    mp->y = (float)(pos_.y + kEyeHeight);
    mp->z = (float)pos_.z;
    mp->yaw = yaw_;
    mp->bodyYaw = yaw_;
    mp->pitch = pitch_;
    mp->mode = network::protocol::MovePlayerPacket::MODE_RESET;
    mp->onGround = onGround();
    sendPacket(mp);
}

// ---------------------------------------------------------------------
// Portal helpers of the target world (search nearby or build).
// ---------------------------------------------------------------------
namespace {

using block::NETHER_PORTAL;

// Linking radius: an existing portal within this distance of the scaled
// coordinates is reused instead of building a new one.
constexpr double kPortalSearchRadius = 128.0;
// Resident-block scan around the target (catches portals the registry
// does not know yet, e.g. lit moments ago by another player).
constexpr int kBlockScanRadius = 8;

struct PortalSpot {
    int32_t x = 0, y = 0, z = 0; // anchor portal block
    uint8_t axis = 0;
    double exit_x = 0, exit_y = 0, exit_z = 0;
    bool has_exit = false;
};

/// @brief safe standing cell beside the portal block at (px,py,pz): a
///        side neighbour with two air blocks over a solid floor.
bool findExitNextTo(world::World* w, int px, int py, int pz, PortalSpot& spot) {
    static const int kOff[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    for (auto& o : kOff) {
        int ax = px + o[0], az = pz + o[1];
        uint8_t legs = w->getBlockId(ax, py, az);
        uint8_t head = w->getBlockId(ax, py + 1, az);
        uint8_t floorb = w->getBlockId(ax, py - 1, az);
        if (legs == block::AIR && head == block::AIR && floorb != block::AIR) {
            spot.exit_x = ax + 0.5;
            spot.exit_y = (double)py;
            spot.exit_z = az + 0.5;
            spot.has_exit = true;
            return true;
        }
    }
    return false;
}

/// @brief anchor of the portal the player is standing in (searches the
///        immediate neighbourhood of the given position).
bool findPortalAround(world::World* w, double px, double py, double pz, PortalArrival& out) {
    int bx = (int)std::floor(px), by = (int)std::floor(py), bz = (int)std::floor(pz);
    for (int dy = -1; dy <= 2; ++dy)
        for (int dx = -1; dx <= 1; ++dx)
            for (int dz = -1; dz <= 1; ++dz) {
                int x = bx + dx, y = by + dy, z = bz + dz;
                if (y < 0 || y > 255)
                    continue;
                if (w->getBlockId(x, y, z) == NETHER_PORTAL) {
                    out = PortalArrival{x, y, z, w->getBlockData(x, y, z)};
                    return true;
                }
            }
    return false;
}

/// @brief adds a portal to the world registry (and persists it) unless
///        the registry already knows it.
void registerPortal(world::World* w, const PortalArrival& p) {
    if (!w || p.x < 0 || w->portals().contains(p.x, p.y, p.z))
        return;
    w->portals().add(p.x, p.y, p.z, p.axis);
    w->savePortals();
}

/// @brief scans resident chunks around (x,z) for a portal block.
///        Discovery only — distant portals come from the registry.
bool scanLoadedPortal(world::World* w, double x, double z, PortalSpot& out) {
    int bx = (int)std::floor(x), bz = (int)std::floor(z);
    for (int dx = -kBlockScanRadius; dx <= kBlockScanRadius; ++dx) {
        for (int dz = -kBlockScanRadius; dz <= kBlockScanRadius; ++dz) {
            int px = bx + dx, pz = bz + dz;
            // Only resident chunks: no cascading synchronous generation
            // during the scan — the registry covers the rest.
            if (!w->isChunkLoaded(px, pz))
                continue;
            for (int yy = 1; yy < 127; ++yy) {
                if (w->getBlockId(px, yy, pz) != NETHER_PORTAL)
                    continue;
                out.x = px;
                out.y = yy;
                out.z = pz;
                out.axis = w->getBlockData(px, yy, pz);
                findExitNextTo(w, px, yy, pz, out);
                return true;
            }
        }
    }
    return false;
}

/// @brief ground level for a new portal column (nether: a solid floor
///        with headroom inside the caverns; overworld: the surface).
int findPortalGroundY(world::World* w, int x, int z, bool nether) {
    if (nether) {
        for (int y = 32; y < 96; ++y) {
            uint8_t ground = w->getBlockId(x, y, z);
            if (ground == block::AIR || ground == block::FLOWING_LAVA || ground == block::LAVA ||
                ground == NETHER_PORTAL)
                continue;
            uint8_t a1 = w->getBlockId(x, y + 1, z);
            uint8_t a2 = w->getBlockId(x, y + 2, z);
            uint8_t a3 = w->getBlockId(x, y + 3, z);
            uint8_t a4 = w->getBlockId(x, y + 4, z);
            if (a1 == block::AIR && a2 == block::AIR && a3 == block::AIR && a4 == block::AIR) {
                return y + 1;
            }
        }
        return 64; // no pocket found: buildPortalAt clears the area
    }
    for (int y = 126; y > 0; --y) {
        uint8_t id = w->getBlockId(x, y, z);
        if (id == block::AIR || id == block::LEAVES || id == block::LEAVES_2)
            continue;
        if (id == block::WATER || id == block::FLOWING_WATER)
            continue;
        if (id == block::LAVA || id == block::FLOWING_LAVA)
            continue;
        return y + 1;
    }
    return 64;
}

/// @brief builds a working portal (4x5 obsidian frame + 2x3 interior
///        along the X axis) with the ground and air around it ready.
///        Returns the portal anchor and the player exit position.
PortalSpot buildPortalAt(world::World* w, int x, int z) {
    // World::setBlock ignores non-resident chunks (anti-freeze rule);
    // the target is usually fresh terrain, so load the 3x3 chunk
    // neighbourhood synchronously before writing.
    int32_t ccx = x >> 4, ccz = z >> 4;
    for (int dx = -1; dx <= 1; ++dx)
        for (int dz = -1; dz <= 1; ++dz)
            w->chunks().getChunk(ccx + dx, ccz + dz);

    bool nether = w->dimension() != 0;
    int gy = findPortalGroundY(w, x, z, nether);

    // 2x3 interior with base at gy; obsidian frame around.
    int x0 = x, z0 = z;
    int y0 = gy;

    // Clear the area (never touching bedrock or the obsidian itself).
    for (int dx = -1; dx <= 4; ++dx) {
        for (int dy = -1; dy <= 5; ++dy) {
            for (int dz = -1; dz <= 1; ++dz) {
                int bx = x0 + dx, by = y0 + dy - 1, bz = z0 + dz;
                uint8_t cur = w->getBlockId(bx, by, bz);
                if (cur == block::AIR || cur == block::BEDROCK || cur == block::OBSIDIAN)
                    continue;
                w->setBlock(bx, by, bz, block::AIR, 0);
            }
        }
    }

    // Frame: base, top and side columns.
    for (int i = 0; i < 4; ++i) {
        w->setBlock(x0 + i, y0 - 1, z0, block::OBSIDIAN, 0);
        w->setBlock(x0 + i, y0 + 3, z0, block::OBSIDIAN, 0);
    }
    for (int j = 0; j < 3; ++j) {
        w->setBlock(x0, y0 + j, z0, block::OBSIDIAN, 0);
        w->setBlock(x0 + 3, y0 + j, z0, block::OBSIDIAN, 0);
    }
    // Interior with portal (meta 0 = X axis).
    for (int i = 1; i <= 2; ++i) {
        for (int j = 0; j < 3; ++j) {
            w->setBlock(x0 + i, y0 + j, z0, NETHER_PORTAL, 0);
        }
    }

    // Firm floor in front of the portal so the exit is not mid-air.
    w->setBlock(x0 + 1, y0 - 1, z0 + 1, block::OBSIDIAN, 0);
    w->setBlock(x0 + 2, y0 - 1, z0 + 1, block::OBSIDIAN, 0);

    PortalSpot spot;
    spot.x = x0 + 1;
    spot.y = y0;
    spot.z = z0;
    spot.axis = 0;
    spot.exit_x = x0 + 1.5;
    spot.exit_y = (double)y0;
    spot.exit_z = z0 + 1.5;
    spot.has_exit = true;
    return spot;
}

} // namespace

// ---------------------------------------------------------------------
// checkPortal — per-tick portal block detection.
// ---------------------------------------------------------------------
// the travel dispara a sola time by entrada to the portal: in_portal_ prevents
// re-disparos while the player siga inside, and last_portal_ms_ applies
// the cooldown anti-rebote justo after leaving to the other lado.
void Player::checkPortal() {
    if (!spawned_.load(std::memory_order_relaxed))
        return;
    world::World* w = getWorld();
    if (!w)
        return;

    int fx = (int)std::floor(pos_.x);
    int fy = (int)std::floor(pos_.y);
    int fz = (int)std::floor(pos_.z);
    uint8_t feet = w->getBlockId(fx, fy, fz);
    uint8_t head = w->getBlockId(fx, fy + 1, fz);
    bool inside = (feet == block::NETHER_PORTAL || head == block::NETHER_PORTAL);
    if (!inside) {
        in_portal_ = false;
        return;
    }
    if (in_portal_)
        return; // still inside of the same portal
    in_portal_ = true;

    uint64_t now = mcp_utils_time_ms();
    if (last_portal_ms_ != 0 && now - last_portal_ms_ < 3000)
        return;
    last_portal_ms_ = now;

    {
        auto ev = event::EventManager::get().fire<event::entity::EntityPortalEnterEvent>(
            static_cast<entity::Entity*>(this), w->dimension() == 0 ? 1 : 0);
        if (ev.isCancelled())
            return;
    }

    if (w->dimension() == 0)
        teleportToNether();
    else
        teleportToOverworld();
}

// ---------------------------------------------------------------------
// Cross-dimension travel.
// ---------------------------------------------------------------------
//
// resolvePortalDestination — finds (or creates) the arrival portal in
// `target` for a trip whose scaled coordinates seed the search at
// (x,y,z). Resolution order, closest to vanilla first:
//   1. persistent registry of the target world (radius 128 blocks),
//   2. portal blocks already resident in memory around the target,
//   3. the remembered link of the last trip through this same portal
//      (keeps round trips exact when the scaled coordinates miss it),
//   4. build a fresh portal at the scaled target.
// On success (x,y,z) holds the exit position and `arrival` the anchor
// block of the arrival portal.
bool Player::resolvePortalDestination(world::World* target, double& x, double& y, double& z,
                                      PortalArrival& arrival) {
    if (!target)
        return false;
    arrival = PortalArrival{};

    auto useSpot = [&](const PortalSpot& spot) {
        arrival = PortalArrival{spot.x, spot.y, spot.z, spot.axis};
        if (spot.has_exit) {
            x = spot.exit_x;
            y = spot.exit_y;
            z = spot.exit_z;
        } else {
            // Unloaded surroundings: stand in the portal block; the
            // in-portal flag keeps this from re-triggering the travel.
            x = spot.x + 0.5;
            y = (double)spot.y;
            z = spot.z + 0.5;
        }
    };

    // 1) Registry: portals discovered on earlier trips (persists across
    //    restarts).
    if (const auto* hit = target->portals().findNearest(x, y, z, kPortalSearchRadius)) {
        // Make the portal's chunk resident before trusting block reads.
        target->chunks().getChunk(hit->x >> 4, hit->z >> 4);
        if (target->getBlockId(hit->x, hit->y, hit->z) == NETHER_PORTAL) {
            PortalSpot spot;
            spot.x = hit->x;
            spot.y = hit->y;
            spot.z = hit->z;
            spot.axis = hit->axis;
            findExitNextTo(target, hit->x, hit->y, hit->z, spot);
            useSpot(spot);
            return true;
        }
        // Stale entry (portal mined away): forget it and keep looking.
        target->portals().remove(hit->x, hit->y, hit->z);
        target->savePortals();
    }

    // 2) Resident portal blocks around the target.
    PortalSpot spot;
    if (scanLoadedPortal(target, x, z, spot)) {
        registerPortal(target, PortalArrival{spot.x, spot.y, spot.z, spot.axis});
        useSpot(spot);
        return true;
    }

    // 3) Remembered link: re-entering the portal of the last trip goes
    //    back through it even when the scaled coordinates would not find
    //    it within the search radius.
    world::World* from = getWorld();
    uint8_t from_dim = from ? from->dimension() : 0;
    if (portal_link_used_ && portal_link_bdim_ == target->dimension() &&
        portal_link_adim_ == from_dim &&
        std::abs(portal_link_ax_ - (int32_t)std::floor(pos_.x)) <= 2 &&
        std::abs(portal_link_ay_ - (int32_t)std::floor(pos_.y)) <= 2 &&
        std::abs(portal_link_az_ - (int32_t)std::floor(pos_.z)) <= 2) {
        target->chunks().getChunk(portal_link_bx_ >> 4, portal_link_bz_ >> 4);
        if (target->getBlockId(portal_link_bx_, portal_link_by_, portal_link_bz_) ==
            NETHER_PORTAL) {
            PortalSpot link;
            link.x = portal_link_bx_;
            link.y = portal_link_by_;
            link.z = portal_link_bz_;
            link.axis = target->getBlockData(link.x, link.y, link.z);
            findExitNextTo(target, link.x, link.y, link.z, link);
            useSpot(link);
            return true;
        }
    }

    // 4) Nothing suitable: build a fresh portal at the scaled target.
    spot = buildPortalAt(target, (int)std::floor(x), (int)std::floor(z));
    registerPortal(target, PortalArrival{spot.x, spot.y, spot.z, spot.axis});
    useSpot(spot);
    return true;
}

void Player::storePortalLink(const PortalArrival& from, uint8_t from_dim, const PortalArrival& to,
                             uint8_t to_dim) {
    portal_link_used_ = from.x >= 0 && to.x >= 0;
    if (!portal_link_used_)
        return;
    portal_link_ax_ = from.x;
    portal_link_ay_ = from.y;
    portal_link_az_ = from.z;
    portal_link_bx_ = to.x;
    portal_link_by_ = to.y;
    portal_link_bz_ = to.z;
    portal_link_adim_ = from_dim;
    portal_link_bdim_ = to_dim;
}

bool Player::teleportToNether() {
    if (!srv_ || !srv_->netherEnabled())
        return false;
    world::World* nether = srv_->getWorldByName("nether");
    if (!nether)
        nether = srv_->loadWorld("nether", "nether");
    if (!nether)
        return false;

    // The world of return is the one being left now.
    world::World* from = getWorld();
    portal_return_ = from;

    // The portal being entered anchors the trip: it joins the source
    // world's registry and becomes the entry end of the travel link.
    PortalArrival entry;
    bool has_entry = from && findPortalAround(from, pos_.x, pos_.y, pos_.z, entry);
    if (has_entry)
        registerPortal(from, entry);

    // Scaled coordinates (8:1 overworld -> nether) seed the search.
    double nx = pos_.x / 8.0;
    double ny = 64.0;
    double nz = pos_.z / 8.0;
    PortalArrival arrival;
    if (!resolvePortalDestination(nether, nx, ny, nz, arrival))
        return false;
    if (has_entry)
        storePortalLink(entry, from->dimension(), arrival, nether->dimension());
    return switchWorld(nether, nx, ny, nz, true);
}

bool Player::teleportToOverworld() {
    if (!srv_)
        return false;
    world::World* target = portal_return_ ? portal_return_ : srv_->getDefaultWorld();
    if (!target)
        return false;
    portal_return_ = nullptr;
    world::World* from = getWorld();

    PortalArrival entry;
    bool has_entry = from && findPortalAround(from, pos_.x, pos_.y, pos_.z, entry);
    if (has_entry)
        registerPortal(from, entry);

    double ox = pos_.x * 8.0;
    double oy = 64.0;
    double oz = pos_.z * 8.0;
    PortalArrival arrival;
    if (!resolvePortalDestination(target, ox, oy, oz, arrival))
        return false;
    if (has_entry)
        storePortalLink(entry, from->dimension(), arrival, target->dimension());
    return switchWorld(target, ox, oy, oz, true);
}

} // namespace cursestone
