/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| |__/| |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
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
// SPDX-License-Identifier: LGPL-3.0-or-later
#include "Player.h"
#include "Server.h"
#include "world/World.h"
#include "nbt/tag/CompoundTag.h"
#include "network/protocol/SetEntityDataPacket.h"
#include "network/protocol/SetSpawnPositionPacket.h"
#include "network/protocol/AnimatePacket.h"
#include "event/EventManager.h"
#include "event/player/PlayerBedEnterEvent.h"
#include "event/player/PlayerBedLeaveEvent.h"

namespace cursestone {

using network::protocol::SetEntityDataPacket;

// Sends sleep metadata to the player and nearby viewers.
// Runtime ID and entity ID 0 are both sent for client compatibility.
static void broadcastSleepMetadata(Player* self, bool sleeping, int bx, int by, int bz) {
    if (!self || !self->server())
        return;

    auto build = [&](int64_t eid) {
        auto pk = std::make_shared<SetEntityDataPacket>();
        pk->eid = eid;

        SetEntityDataPacket::Entry bedPos;
        bedPos.index = SetEntityDataPacket::PROP_PLAYER_BED_POSITION;
        bedPos.type = SetEntityDataPacket::TYPE_POS;
        bedPos.px = bx;
        bedPos.py = by;
        bedPos.pz = bz;
        pk->entries.push_back(bedPos);

        SetEntityDataPacket::Entry flags;
        flags.index = SetEntityDataPacket::PROP_PLAYER_FLAGS;
        flags.type = SetEntityDataPacket::TYPE_BYTE;
        flags.ival = sleeping ? SetEntityDataPacket::PLAYER_FLAG_SLEEP : 0;
        pk->entries.push_back(flags);
        return pk;
    };

    self->sendPacket(build(self->runtimeId()));
    self->sendPacket(build(0));

    auto viewerPk = build(self->runtimeId());
    for (auto& other : self->server()->playersSnapshot()) {
        if (!other || other.get() == self)
            continue;
        if (other->isPendingRemoval())
            continue;
        if (!other->spawned())
            continue;
        if (other->getWorld() != self->getWorld())
            continue;
        other->sendPacket(viewerPk);
    }
}

// Returns false if the player cannot sleep or the enter event is cancelled.
bool Player::sleepOn(int bx, int by, int bz) {
    if (!spawned_.load(std::memory_order_relaxed))
        return false;
    if (isDead())
        return false;
    if (!srv_)
        return false;

    // Prevent multiple players from sleeping in the same bed.
    for (auto& other : srv_->playersSnapshot()) {
        if (!other || other.get() == this)
            continue;
        if (other->isPendingRemoval())
            continue;
        if (!other->isSleeping())
            continue;
        if (other->getWorld() != getWorld())
            continue;
        int ox, oy, oz;
        other->sleepPos(ox, oy, oz);
        double dx = (double)ox - bx, dy = (double)oy - by, dz = (double)oz - bz;
        if (dx * dx + dy * dy + dz * dz <= 0.01)
            return false;
    }

    // Allow plugins to cancel entering a bed.
    {
        auto ev =
            event::EventManager::get().fire<event::player::PlayerBedEnterEvent>(this, bx, by, bz);
        if (ev.isCancelled())
            return false;
    }

    sleeping_ = true;
    sleep_x_ = bx;
    sleep_y_ = by;
    sleep_z_ = bz;

    broadcastSleepMetadata(this, true, bx, by, bz);

    // Set the personal spawn point if bed spawning is enabled.
    if (!srv_ || srv_->bedSpawnEnabled())
        setSpawn(bx, by, bz, /*save*/ true);

    // Start the 60-tick countdown used to skip the night.
    if (World* lvl = getWorld())
        lvl->setSleepTicks(60);

    if (srv_ && srv_->debugMode()) {
        utils::MainLogger::get().info("[SLEEP] " + cfg_.username + " is sleeping at " +
                                      std::to_string(bx) + "," + std::to_string(by) + "," +
                                      std::to_string(bz));
    }
    return true;
}

void Player::stopSleep() {
    if (!sleeping_)
        return;

    // Notify plugins that the player is leaving the bed.
    (void)event::EventManager::get().fire<event::player::PlayerBedLeaveEvent>(this, sleep_x_,
                                                                              sleep_y_, sleep_z_);

    sleeping_ = false;

    // Clear the bed position and sleep flag.
    broadcastSleepMetadata(this, false, 0, 0, 0);

    // Cancel the night-skip countdown.
    if (World* lvl = getWorld())
        lvl->setSleepTicks(0);

    // Send the wake-up animation to the player.
    for (int64_t eid : {(int64_t)runtimeId(), (int64_t)0}) {
        auto anim = std::make_shared<network::protocol::AnimatePacket>();
        anim->eid = eid;
        anim->action = network::protocol::ANIMATE_ACTION_WAKE_UP;
        sendPacket(anim);
    }

    if (srv_ && srv_->debugMode()) {
        utils::MainLogger::get().info("[SLEEP] " + cfg_.username + " stopped sleeping");
    }
}

// Wake players sleeping in a bed when either half is broken.
// The stored sleep position is the head, so check the broken block
// and its four horizontal neighbors.
void Player::wakeSleepersOnBed(Server* srv, world::World* lvl, int bx, int by, int bz) {
    if (!srv || !lvl)
        return;
    for (auto& other : srv->playersSnapshot()) {
        if (!other || other->isPendingRemoval())
            continue;
        if (!other->isSleeping())
            continue;
        if (other->getWorld() != lvl)
            continue;
        int ox, oy, oz;
        other->sleepPos(ox, oy, oz);
        bool onThisBed = (ox == bx && oy == by && oz == bz) ||
                         (ox == bx + 1 && oy == by && oz == bz) ||
                         (ox == bx - 1 && oy == by && oz == bz) ||
                         (ox == bx && oy == by && oz == bz + 1) ||
                         (ox == bx && oy == by && oz == bz - 1);
        if (onThisBed)
            other->stopSleep();
    }
}

// Clear the personal spawn override and restore the world spawn.
void Player::resetSpawnToDefault() {
    has_spawn_override_ = false;
    spawn_x_ = 0;
    spawn_y_ = 0;
    spawn_z_ = 0;

    if (named_tag_) {
        cursestone::nbt::remove(named_tag_.get(), "SpawnX");
        cursestone::nbt::remove(named_tag_.get(), "SpawnY");
        cursestone::nbt::remove(named_tag_.get(), "SpawnZ");
    }

    math::Vector3 sp = getWorld() ? getWorld()->spawn() : math::Vector3{0, 64, 0};

    auto pk = std::make_shared<network::protocol::SetSpawnPositionPacket>();
    pk->x = (int)sp.x;
    pk->y = (int)sp.y;
    pk->z = (int)sp.z;
    sendPacket(pk);
}

// Reset spawn points that reference a bed when either half is broken.
void Player::clearSpawnsOnBed(Server* srv, world::World* lvl, int bx, int by, int bz) {
    if (!srv || !lvl)
        return;
    for (auto& other : srv->playersSnapshot()) {
        if (!other || other->isPendingRemoval())
            continue;
        if (!other->hasSpawnOverride())
            continue;
        if (other->getWorld() != lvl)
            continue;
        int sx, sy, sz;
        other->spawnPos(sx, sy, sz);
        bool onThisBed = (sx == bx && sy == by && sz == bz) ||
                         (sx == bx + 1 && sy == by && sz == bz) ||
                         (sx == bx - 1 && sy == by && sz == bz) ||
                         (sx == bx && sy == by && sz == bz + 1) ||
                         (sx == bx && sy == by && sz == bz - 1);
        if (onThisBed)
            other->resetSpawnToDefault();
    }
}

// Store the personal spawn point, update the client, and optionally save it to NBT.
void Player::setSpawn(int x, int y, int z, bool save) {
    has_spawn_override_ = true;
    spawn_x_ = x;
    spawn_y_ = y;
    spawn_z_ = z;

    auto pk = std::make_shared<network::protocol::SetSpawnPositionPacket>();
    pk->x = x;
    pk->y = y;
    pk->z = z;
    sendPacket(pk);

    if (save && named_tag_ && srv_ && !cfg_.username.empty()) {
        cursestone::nbt::set_int(named_tag_.get(), "SpawnX", x);
        cursestone::nbt::set_int(named_tag_.get(), "SpawnY", y);
        cursestone::nbt::set_int(named_tag_.get(), "SpawnZ", z);
    }
}

} // namespace cursestone