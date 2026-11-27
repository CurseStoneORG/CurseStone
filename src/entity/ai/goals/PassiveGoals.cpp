/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___/

 * CurseStone | Open Source Server Software
 *
 * Copyright (C) 2026 CurseStoneORG
 *
 * Developed by CurseStoneORG.
 * Built for an open and extensible Minecraft server ecosystem.
 *
 * Licensed under the GNU Lesser General Public License v3.0 or later.
 * You may study, modify, and redistribute this software under its license.
 *
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 * Repository: https://github.com/CurseStoneORG/CurseStone
 *
 */
// SPDX-License-Identifier: LGPL-3.0-or-later
#include "entity/ai/goals/PassiveGoals.h"
#include "Server.h"
#include "Player.h"
#include "inventory/PlayerInventory.h"
#include "item/ItemStack.h"
#include "block/BlockIds.h"
#include "entity/ai/AiMath.h"
#include "entity/ai/LookControl.h"
#include "entity/ai/PathNavigation.h"
#include "entity/ai/RandomPos.h"
#include "network/protocol/EntityEventPacket.h"
#include "world/World.h"
#include <cmath>

namespace cursestone::entity::ai {

// ---- PanicGoal ----

bool PanicGoal::canUse(Mob&) {
    // Era: the last hurt source arms the panic.
    TargetRef attacker = holder_.getLastHurtByMob();
    if (!attacker)
        return false;
    if (!generateRandomPos(target_x_, target_y_, target_z_, holder_, 5, 4))
        return false;
    return true;
}

bool PanicGoal::canContinueToUse(Mob&) {
    return holder_.getNavigation() && !holder_.getNavigation()->isDone();
}

void PanicGoal::start(Mob&) {
    if (holder_.getNavigation())
        holder_.getNavigation()->moveTo(target_x_, target_y_, target_z_,
                                        speed_multiplier_);
}

// ---- TemptGoal ----

namespace {
/// @brief The held item id of a player, or 0 when the hand is empty.
uint16_t heldItemId(const TargetRef& player) {
    if (!player || !player.player)
        return 0;
    for (auto& p : Server::getInstance().playersSnapshotRef()) {
        if (p && p->runtimeId() == player.eid) {
            const item::ItemStack held = p->getInventory()->getHeldItem();
            return held.id;
        }
    }
    return 0;
}
} // namespace

bool TemptGoal::canUse(Mob&) {
    // Era: while the timeout runs, the goal stays quiet.
    if (timeout_ > 0) {
        --timeout_;
        return false;
    }
    TargetRef nearest = holder_.nearestPlayer(10.0);
    if (!nearest) {
        followed_.clear();
        return false;
    }
    if (nearest.eid != followed_.eid)
        followed_ = nearest;
    const uint16_t held = heldItemId(followed_);
    for (const uint16_t id : follow_item_ids_) {
        if (id == held)
            return true;
    }
    return false;
}

bool TemptGoal::canContinueToUse(Mob& mob) {
    holder_.resolve(followed_);
    if (!followed_)
        return false;
    if (scared_of_movement_) {
        // Era field_4C: a player within 6 blocks that looks or moves
        // breaks the temptation.
        const double distSqr =
            holder_.distanceToSqr(followed_.x, followed_.y, followed_.z);
        if (distSqr < 36.0) {
            const double moved =
                (followed_.x - last_px_) * (followed_.x - last_px_) +
                (followed_.y - last_py_) * (followed_.y - last_py_) +
                (followed_.z - last_pz_) * (followed_.z - last_pz_);
            if (moved > 0.16 || std::fabs(followed_.yaw - last_yaw_) > 5.0f)
                return false;
        } else {
            last_px_ = followed_.x;
            last_py_ = followed_.y;
            last_pz_ = followed_.z;
        }
        last_yaw_ = followed_.yaw;
        last_pitch_ = followed_.pitch;
        (void)last_pitch_;
    }
    return canUse(mob);
}

void TemptGoal::start(Mob&) {
    holder_.resolve(followed_);
    last_px_ = followed_.x;
    last_py_ = followed_.y;
    last_pz_ = followed_.z;
    running_ = true;
    // Era start(): temptation ignores water.
    if (holder_.getNavigation())
        holder_.getNavigation()->setAvoidWater(false);
}

void TemptGoal::stop(Mob&) {
    followed_.clear();
    if (holder_.getNavigation()) {
        holder_.getNavigation()->stop();
        // Era stop(): the navigation's avoid-water flag returns to the
        // value the goal saved at start (the era default is off).
        holder_.getNavigation()->setAvoidWater(false);
    }
    timeout_ = 100;
    running_ = false;
}

void TemptGoal::tick(Mob&) {
    holder_.resolve(followed_);
    if (!followed_ || !holder_.getNavigation())
        return;
    LookControl* look = holder_.getLookControl();
    if (look)
        look->setLookAt(followed_.x, followed_.y + followed_.eyeHeight,
                        followed_.z, 30.0f, holder_.getMaxHeadXRot());

    const double distSqr =
        holder_.distanceToSqr(followed_.x, followed_.y, followed_.z);
    if (distSqr < 6.25) {
        holder_.getNavigation()->stop();
        return;
    }
    // Era re-pathed on its Mth::fastRandom() delay (the body is lost to
    // the decompile); a bounded 10-tick cadence keeps the follow tight.
    if (--repath_ticks_ <= 0) {
        repath_ticks_ = 10;
        holder_.getNavigation()->moveToEntity(followed_, speed_multiplier_);
    }
}

// ---- EatTileGoal ----

bool EatTileGoal::canUse(Mob&) {
    World* level = holder_.level();
    if (!level)
        return false;
    // Era: a 1-in-1000 roll (1-in-50 for a baby — no babies here).
    mcp_random_t& rng = holder_.random();
    if (mcp_random_next_u32(&rng) % 1000u != 0u)
        return false;
    const int32_t x = math::floorToInt(holder_.posX());
    const int32_t y = math::floorToInt(holder_.posY());
    const int32_t z = math::floorToInt(holder_.posZ());
    // Era: tall grass on the feet cell, or grass under it.
    const uint8_t feet = level->getBlockId(x, y, z);
    if (feet == block::TALL_GRASS)
        return true;
    return level->getBlockId(x, y - 1, z) == block::GRASS;
}

void EatTileGoal::start(Mob&) {
    ticks_left_ = 40;
    // Era broadcastEntityEvent(holder, 10): the eat animation.
    if (holder_.level())
        holder_.level()->mobEntityEvent(holder_.eid(), (int32_t)holder_.posX() >> 4,
                                        (int32_t)holder_.posZ() >> 4,
                                        network::protocol::EntityEventPacket::
                                            EAT_GRASS_ANIMATION);
    if (holder_.getNavigation())
        holder_.getNavigation()->stop();
}

void EatTileGoal::stop(Mob&) {
    ticks_left_ = 0;
}

void EatTileGoal::tick(Mob&) {
    if (ticks_left_ > 0)
        --ticks_left_;
    if (ticks_left_ != 4)
        return;
    World* level = holder_.level();
    if (!level)
        return;
    const int32_t x = math::floorToInt(holder_.posX());
    const int32_t y = math::floorToInt(holder_.posY()) - 1;
    const int32_t z = math::floorToInt(holder_.posZ());
    // Era: the grass under the body turns to dirt on tick 4.
    if (level->getBlockId(x, y, z) == block::GRASS)
        level->setBlock(x, y, z, block::DIRT, 0);
}

} // namespace cursestone::entity::ai
