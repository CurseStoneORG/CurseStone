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
// the sequence PMMP/Genisys 0.15 used: SetTime, ChangeDimension,
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
    if (world_) return world_;
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
bool Player::switchWorld(World* target, double x, double y, double z,
                         bool throughPortal) {
    (void)throughPortal;
    if (!srv_ || !target) return false;
    World* from = getWorld();

    {
        auto ev = event::EventManager::get().fire<event::player::PlayerTeleportEvent>(
            this, pos_.x, pos_.y, pos_.z, x, y, z);
        if (ev.isCancelled()) return false;
        x = ev.toX; y = ev.toY; z = ev.toZ;
    }

    // The open container belongs to the previous world.
    closeOpenChest();

    bool dimension_change = !from || target->dimension() != from->dimension();

    world_ = target;
    pos_.x = x; pos_.y = y; pos_.z = z;
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
        return true;   // pre-spawn: doFirstSpawn() sends the initial packets
    }

    if (dimension_change) {
        // PMMP 0.15 switchLevel() order: SetTime first, then
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
        mp->y = (float)(y + kEyeHeight);   // on the wire Y is the eye height
        mp->z = (float)z;
        mp->yaw = yaw_; mp->bodyYaw = yaw_; mp->pitch = pitch_;
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
// like PMMP's checkTeleportPosition() flow.
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
    mp->yaw = yaw_; mp->bodyYaw = yaw_; mp->pitch = pitch_;
    mp->mode = network::protocol::MovePlayerPacket::MODE_RESET;
    mp->onGround = onGround();
    sendPacket(mp);
}

// ---------------------------------------------------------------------
// Helpers of portal in the world target (look for uno near or build).
// ---------------------------------------------------------------------
namespace {

using block::NETHER_PORTAL;

/// @brief looks for a block of portal in chunks already loaded around of
///        (x,z) and returns in (x,and,z) a position of exit safe
///        next to to él. not forces generation of chunks.
bool findPortalNear(world::World* w, double& x, double& y, double& z) {
    const int R = 8;
    int bx = (int)std::floor(x);
    int bz = (int)std::floor(z);
    for (int dx = -R; dx <= R; ++dx) {
        for (int dz = -R; dz <= R; ++dz) {
            int px = bx + dx, pz = bz + dz;
            // only columns of chunks loaded: avoid generaciones
            // síncronas in cascada during the búsqueda.
            if (!w->isChunkLoaded(px, pz)) continue;
            for (int yy = 1; yy < 126; ++yy) {
                if (w->getBlockId(px, yy, pz) != NETHER_PORTAL) continue;
                // exit: cell free next to to the portal (same ground).
                static const int kOff[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};
                for (auto& o : kOff) {
                    int ax = px + o[0], az = pz + o[1];
                    uint8_t legs = w->getBlockId(ax, yy, az);
                    uint8_t head = w->getBlockId(ax, yy + 1, az);
                    uint8_t floorb = w->getBlockId(ax, yy - 1, az);
                    if (legs == block::AIR && head == block::AIR &&
                        floorb != block::AIR) {
                        x = ax + 0.5;
                        y = (double)yy;
                        z = az + 0.5;
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

/// @brief and safe for construir a portal in the columna (x,z).
///        in the Nether looks for a hueco of air about ground solid; in the
///        surface baja from the sky until the primer block firme.
int findPortalGroundY(world::World* w, int x, int z, bool nether) {
    if (nether) {
        for (int y = 32; y < 96; ++y) {
            uint8_t ground = w->getBlockId(x, y, z);
            if (ground == block::AIR || ground == block::FLOWING_LAVA ||
                ground == block::LAVA || ground == block::NETHER_PORTAL) continue;
            uint8_t a1 = w->getBlockId(x, y + 1, z);
            uint8_t a2 = w->getBlockId(x, y + 2, z);
            uint8_t a3 = w->getBlockId(x, y + 3, z);
            uint8_t a4 = w->getBlockId(x, y + 4, z);
            if (a1 == block::AIR && a2 == block::AIR &&
                a3 == block::AIR && a4 == block::AIR) {
                return y + 1;
            }
        }
        return 64;   // without hueco: buildPortal despeja the zona
    }
    for (int y = 126; y > 0; --y) {
        uint8_t id = w->getBlockId(x, y, z);
        if (id == block::AIR || id == block::LEAVES || id == block::LEAVES_2) continue;
        if (id == block::WATER || id == block::FLOWING_WATER) continue;
        if (id == block::LAVA || id == block::FLOWING_LAVA) continue;
        return y + 1;
    }
    return 64;
}

/// @brief places a portal funcional (frame 4x5 + interior of portal to
///        it along of the axis X) with the ground and the air around listos.
///        returns the position where must aparecer the player.
void buildPortalAt(world::World* w, int x, int z, double& ox, double& oy, double& oz) {
    // World::setBlock ignora chunks not resident (decisión anti-freeze);
    // the target suele be terrain recién generado, this way that the vecindario
    // 3x3 of chunks is load here of way síncrona before write.
    int32_t ccx = x >> 4, ccz = z >> 4;
    for (int dx = -1; dx <= 1; ++dx) {
        for (int dz = -1; dz <= 1; ++dz) {
            w->chunks().getChunk(ccx + dx, ccz + dz);
        }
    }

    bool nether = w->dimension() != 0;
    int gy = findPortalGroundY(w, x, z, nether);

    // interior 2x3 with base in gy; frame of obsidian around.
    int x0 = x, z0 = z;
    int y0 = gy;

    // Despejar the zona (without touch bedrock ni the own obsidian).
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

    // frame: base, techo and laterales.
    for (int i = 0; i < 4; ++i) {
        w->setBlock(x0 + i, y0 - 1, z0, block::OBSIDIAN, 0);
        w->setBlock(x0 + i, y0 + 3, z0, block::OBSIDIAN, 0);
    }
    for (int j = 0; j < 3; ++j) {
        w->setBlock(x0, y0 + j, z0, block::OBSIDIAN, 0);
        w->setBlock(x0 + 3, y0 + j, z0, block::OBSIDIAN, 0);
    }
    // interior with portal (meta 0 = axis X).
    for (int i = 1; i <= 2; ++i) {
        for (int j = 0; j < 3; ++j) {
            w->setBlock(x0 + i, y0 + j, z0, NETHER_PORTAL, 0);
        }
    }

    // Piso firme in front to the portal so that the exit not is to the empty.
    w->setBlock(x0 + 1, y0 - 1, z0 + 1, block::OBSIDIAN, 0);
    w->setBlock(x0 + 2, y0 - 1, z0 + 1, block::OBSIDIAN, 0);

    ox = x0 + 1.5;
    oy = (double)y0;
    oz = z0 + 1.5;
}

} // namespace

// ---------------------------------------------------------------------
// checkPortal — detección per tick of the block of portal.
// ---------------------------------------------------------------------
// the travel dispara a sola time by entrada to the portal: in_portal_ prevents
// re-disparos while the player siga inside, and last_portal_ms_ applies
// the cooldown anti-rebote justo after leaving to the other lado.
void Player::checkPortal() {
    if (!spawned_.load(std::memory_order_relaxed)) return;
    world::World* w = getWorld();
    if (!w) return;

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
    if (in_portal_) return;   // still inside of the same portal
    in_portal_ = true;

    uint64_t now = mcp_utils_time_ms();
    if (last_portal_ms_ != 0 && now - last_portal_ms_ < 3000) return;
    last_portal_ms_ = now;

    {
        auto ev = event::EventManager::get().fire<event::entity::EntityPortalEnterEvent>(
            static_cast<entity::Entity*>(this), w->dimension() == 0 ? 1 : 0);
        if (ev.isCancelled()) return;
    }

    if (w->dimension() == 0) teleportToNether();
    else teleportToOverworld();
}

// ---------------------------------------------------------------------
// Viajes dimensionales.
// ---------------------------------------------------------------------
bool Player::teleportToNether() {
    if (!srv_ || !srv_->netherEnabled()) return false;
    world::World* nether = srv_->getWorldByName("nether");
    if (!nether) nether = srv_->loadWorld("nether", "nether");
    if (!nether) return false;

    // the world of regreso is that which estamos abandonando now.
    portal_return_ = getWorld();

    double nx = pos_.x / 8.0;
    double ny = 64.0;
    double nz = pos_.z / 8.0;
    if (!findPortalNear(nether, nx, ny, nz)) {
        buildPortalAt(nether, (int)std::floor(nx), (int)std::floor(nz),
                      nx, ny, nz);
    }
    return switchWorld(nether, nx, ny, nz, true);
}

bool Player::teleportToOverworld() {
    if (!srv_) return false;
    world::World* target = portal_return_ ? portal_return_
                                          : srv_->getDefaultWorld();
    if (!target) return false;
    portal_return_ = nullptr;

    double ox = pos_.x * 8.0;
    double oy = 64.0;
    double oz = pos_.z * 8.0;
    if (!findPortalNear(target, ox, oy, oz)) {
        buildPortalAt(target, (int)std::floor(ox), (int)std::floor(oz),
                      ox, oy, oz);
    }
    return switchWorld(target, ox, oy, oz, true);
}

} // namespace cursestone
