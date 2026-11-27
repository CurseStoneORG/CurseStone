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
#include "entity/ai/goals/DoorGoals.h"
#include "Server.h"
#include "block/BlockIds.h"
#include "entity/ai/AiMath.h"
#include "entity/ai/PathNavigation.h"
#include "world/World.h"
#include <algorithm>

namespace cursestone::entity::ai {

bool DoorInteractGoal::isDoorCell(int32_t x, int32_t y, int32_t z) const {
    World* level = holder_.level();
    return level && level->getBlockId(x, y, z) == block::WOODEN_DOOR;
}

bool DoorInteractGoal::canUse(Mob&) {
    // Era gate 1: the body must be clipping horizontally.
    if (!holder_.isCollidedHorizontally())
        return false;
    PathNavigation* nav = holder_.getNavigation();
    if (!nav || !nav->getPath())
        return false;
    // Era gate 2: an active path and the door permission flag.
    if (nav->isDone() || !nav->canOpenDoors())
        return false;
    // Era scan: the next three path cells within 1.5 blocks.
    const Path* path = nav->getPath();
    const int32_t end =
        std::min(path->getIndex() + 2, path->getSize()) - 1;
    for (int32_t i = path->getIndex(); i <= end; ++i) {
        const PathNode* n = path->get(i);
        if (!n)
            continue;
        door_x_ = n->xCoord;
        door_y_ = n->yCoord;
        door_z_ = n->zCoord;
        if (holder_.distanceToSqr((double)door_x_, holder_.posY(),
                                  (double)door_z_) <= 2.25) {
            // The era probes the upper door cell; the port accepts the
            // lower cell too (the server door spans both).
            if (isDoorCell(door_x_, door_y_, door_z_) ||
                isDoorCell(door_x_, door_y_ + 1, door_z_)) {
                return true;
            }
        }
    }
    // Era fallback: the door the body stands against right now.
    door_x_ = math::floorToInt(holder_.posX());
    door_y_ = math::floorToInt(holder_.posY() + 1.0);
    door_z_ = math::floorToInt(holder_.posZ());
    return isDoorCell(door_x_, door_y_, door_z_);
}

void DoorInteractGoal::start(Mob&) {
    close_to_target_ = false;
    target_x_ = (double)door_x_ + 0.5 - holder_.posX();
    target_z_ = (double)door_z_ + 0.5 - holder_.posZ();
}

void DoorInteractGoal::tick(Mob&) {
    // Era: the goal ends once the body crossed the door plane.
    const double dx = (double)door_x_ + 0.5 - holder_.posX();
    const double dz = (double)door_z_ + 0.5 - holder_.posZ();
    if (dx * target_x_ + dz * target_z_ < 0.0)
        close_to_target_ = true;
}

bool BreakDoorGoal::canUse(Mob& mob) {
    // Era: the interact gate plus a closed door at the probe cell.
    return DoorInteractGoal::canUse(mob);
}

bool BreakDoorGoal::canContinueToUse(Mob&) {
    World* level = holder_.level();
    const double distSqr = holder_.distanceToSqr((double)door_x_,
                                                 (double)door_y_,
                                                 (double)door_z_);
    // Era: the pounding runs while the timer holds, the door still
    // stands and the body stays within two blocks.
    return ticks_left_ >= 0 && level &&
           level->getBlockId(door_x_, door_y_, door_z_) == block::WOODEN_DOOR &&
           distSqr < 4.0;
}

void BreakDoorGoal::start(Mob& mob) {
    DoorInteractGoal::start(mob);
    ticks_left_ = 240;
}

void BreakDoorGoal::tick(Mob& mob) {
    DoorInteractGoal::tick(mob);
    if (ticks_left_ > 0)
        --ticks_left_;
    if (ticks_left_ != 0)
        return;
    World* level = holder_.level();
    if (!level)
        return;
    // Era: only hard difficulty breaks the door.
    if (Server::getInstance().difficulty() != 3)
        return;
    // Both door halves drop (the era broke the upper cell; the server
    // door is a two-cell pair).
    if (level->getBlockId(door_x_, door_y_, door_z_) == block::WOODEN_DOOR)
        level->setBlock(door_x_, door_y_, door_z_, 0, 0);
    if (level->getBlockId(door_x_, door_y_ + 1, door_z_) == block::WOODEN_DOOR)
        level->setBlock(door_x_, door_y_ + 1, door_z_, 0, 0);
    if (level->getBlockId(door_x_, door_y_ - 1, door_z_) == block::WOODEN_DOOR)
        level->setBlock(door_x_, door_y_ - 1, door_z_, 0, 0);
}

} // namespace cursestone::entity::ai
