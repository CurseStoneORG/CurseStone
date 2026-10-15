/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
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

// Sends the bed position and sleep flag to the viewers (runtime id) and to
// the player itself, like Infinity Player::setDataProperty(). StartGame
// tells the client its own id is the runtime id, so the self copy uses it;
// the eid 0 copy covers clients that treat 0 as "me".
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

// ============================================================
// Player::sleepOn — Infinity Player::sleepOn() (Player.php:1250)
// ============================================================
// Returns true when the player went to sleep. False = occupied bed or a
// plugin cancelled PlayerBedEnterEvent; the caller (the bed interact
// branch of handleBlockPlace) then shows "This bed is occupied".
bool Player::sleepOn(int bx, int by, int bz) {
    if (!spawned_.load(std::memory_order_relaxed))
        return false;
    if (isDead())
        return false;
    if (!srv_)
        return false;

    // Occupancy check — Infinity scans the entities near the player's
    // bounding box (grow 2,1,2) for ANOTHER player already sleeping at
    // this exact bed (distance <= 0.1). We keep the same semantics but
    // resolve it directly against the player list: two players can never
    // sleep in the same bed.
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

    // PlayerBedEnterEvent — cancellable. A cancelled event aborts the
    // sleep (and the caller reports the bed as occupied, same message
    // flow Infinity uses for any sleepOn() == false).
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

    // Sleeping re-binds the personal spawn point to the bed.
    setSpawn(bx, by, bz, /*save*/ true);

    // Arm the level's skip-the-night countdown (Infinity
    // Player.php:1280 `$this->level->sleepTicks = 60;`): after 3 seconds
    // of everyone sleeping, World::checkSleep() cuts the night.
    if (World* lvl = getWorld())
        lvl->setSleepTicks(60);

    if (srv_ && srv_->advancedLog()) {
        utils::MainLogger::get().info("[SLEEP] " + cfg_.username + " is sleeping at " +
                                      std::to_string(bx) + "," + std::to_string(by) + "," +
                                      std::to_string(bz));
    }
    return true;
}

// ============================================================
// Player::stopSleep — Infinity Player::stopSleep() (Player.php:1301)
// ============================================================
void Player::stopSleep() {
    if (!sleeping_)
        return;

    // PlayerBedLeaveEvent (not cancellable — you cannot refuse to wake up).
    (void)event::EventManager::get().fire<event::player::PlayerBedLeaveEvent>(this, sleep_x_,
                                                                              sleep_y_, sleep_z_);

    sleeping_ = false;

    // Clear the bed position and the sleep flag.
    broadcastSleepMetadata(this, false, 0, 0, 0);

    // Cancel the pending night-skip vote (Infinity: sleepTicks = 0).
    if (World* lvl = getWorld())
        lvl->setSleepTicks(0);

    // WAKE_UP animation to the player itself (Infinity
    // Player.php:1235-1238 sends AnimatePacket with action 3 via
    // $this->dataPacket — self only; the other clients already saw the
    // flag byte clear).
    for (int64_t eid : {(int64_t)runtimeId(), (int64_t)0}) {
        auto anim = std::make_shared<network::protocol::AnimatePacket>();
        anim->eid = eid;
        anim->action = network::protocol::ANIMATE_ACTION_WAKE_UP;
        sendPacket(anim);
    }

    if (srv_ && srv_->advancedLog()) {
        utils::MainLogger::get().info("[SLEEP] " + cfg_.username + " stopped sleeping");
    }
}

// ============================================================
// Player::wakeSleepersOnBed — vanilla nicety Infinity's bed onBreak
// doesn't do explicitly (the sleeper just keeps a dead bed reference).
// Breaking either half of a bed wakes everyone sleeping in it, so the
// sleeper is never stuck "sleeping" in a bed that no longer exists.
//
// sleepOn() stores the HEAD half. The broken half (bx,by,bz) may be the
// head itself or the foot: the foot is always one of the four horizontal
// neighbours of the head, so matching (bx,by,bz) plus its four
// horizontal neighbours covers both cases without touching bed meta.
// ============================================================
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
        bool onThisBed = (ox == bx && oy == by && oz == bz) ||     // broken the head
                         (ox == bx + 1 && oy == by && oz == bz) || // broken the foot (east)
                         (ox == bx - 1 && oy == by && oz == bz) || // broken the foot (west)
                         (ox == bx && oy == by && oz == bz + 1) || // broken the foot (south)
                         (ox == bx && oy == by && oz == bz - 1);   // broken the foot (north)
        if (onThisBed)
            other->stopSleep();
    }
}

// ============================================================
// Player::setSpawn — Infinity Player::setSpawn() (Player.php:1283)
// ============================================================
// Stores the personal spawn point and pushes SetSpawnPositionPacket so
// the client's compass/respawn agree with the server. `save` also
// persists it into the player NBT (Infinity Human::saveNBT writes
// SpawnX/SpawnY/SpawnZ).
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
        mcp_compound_tag_set_int(named_tag_.get(), "SpawnX", x);
        mcp_compound_tag_set_int(named_tag_.get(), "SpawnY", y);
        mcp_compound_tag_set_int(named_tag_.get(), "SpawnZ", z);
    }
}

} // namespace cursestone
