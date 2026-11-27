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
#include "entity/ai/PathNavigation.h"
#include "entity/ai/AiMath.h"
#include "entity/ai/MoveControl.h"
#include "entity/ai/Mob.h"
#include "world/World.h"
#include <cmath>

namespace cursestone::entity::ai {

PathNavigation::~PathNavigation() = default;

bool PathNavigation::canUpdatePath() const {
    if (!mob_)
        return false;
    if (mob_->onGround())
        return true;
    if (!finder_.canFloat())
        return false;
    return mob_->isInWater() || mob_->isInLava();
}

bool PathNavigation::moveToEntity(const TargetRef& target, float speed) {
    if (!target)
        return false;
    return moveTo(target.x, target.y, target.z, speed);
}

bool PathNavigation::moveTo(double x, double y, double z, float speed) {
    if (!canUpdatePath())
        return false;

    // Re-path rate limit: reuse the current path while its goal still
    // stands (see the header note). The era re-ran A* on every request.
    if (last_goal_valid_ && path_ && !path_->isDone()) {
        const double dx = x - last_goal_x_;
        const double dy = y - last_goal_y_;
        const double dz = z - last_goal_z_;
        const bool sameGoal = (dx * dx + dy * dy + dz * dz) < 2.25;
        const bool fresh =
            mob_ && (int32_t)mob_->tick() - last_path_tick_ < kRepathTicks;
        if (sameGoal && fresh) {
            speed_multiplier_ = speed;
            return true;
        }
    }

    std::unique_ptr<Path> computed = finder_.findPath(*mob_, x, y, z, range_);
    if (!computed) {
        // Era: a failed search drops the old path.
        stop();
        return false;
    }
    last_goal_x_ = x;
    last_goal_y_ = y;
    last_goal_z_ = z;
    last_goal_valid_ = true;
    last_path_tick_ = mob_ ? (int32_t)mob_->tick() : 0;
    speed_multiplier_ = speed;
    if (avoid_sun_)
        trimPathFromSun();
    if (path_ && computed->sameAs(path_.get()))
        return true; // Same route: keep the walk index.
    path_ = std::move(computed);
    return path_->getSize() > 0;
}

bool PathNavigation::moveToPath(std::unique_ptr<Path> path, float speed) {
    if (!path) {
        path_.reset();
        return false;
    }
    if (path_ && path->sameAs(path_.get()))
        return true; // Era: same path -> keep the walk index.
    path_ = std::move(path);
    speed_multiplier_ = speed;
    last_goal_valid_ = false;
    return path_->getSize() > 0;
}

void PathNavigation::tick() {
    ++age_ticks_;
    if (!mob_ || isDone())
        return;
    if (!canUpdatePath())
        return;
    updatePath();
    if (!path_ || path_->isDone())
        return;
    const Path::WalkPos pos = path_->currentPos(mob_->entityWidth());
    mob_->getMoveControl()->setWantedPosition(pos.x, pos.y, pos.z,
                                              speed_multiplier_);
}

int32_t PathNavigation::getSurfaceY() const {
    // Era getSurfaceY: the body-center cell, or the top of the water
    // column when the body floats.
    if (!mob_->isInWater() || !finder_.canFloat())
        return math::floorToInt(mob_->posY() + 0.5);
    int32_t y = math::floorToInt(mob_->posY());
    const int32_t maxY = y + 17;
    const int32_t x = math::floorToInt(mob_->posX());
    const int32_t z = math::floorToInt(mob_->posZ());
    uint8_t id = level_->getBlockId(x, y, z);
    while (source_.isWater(id)) {
        ++y;
        if (y >= maxY)
            return math::floorToInt(mob_->posY());
        id = level_->getBlockId(x, y, z);
    }
    return y;
}

void PathNavigation::updatePath() {
    if (!path_)
        return;
    const float width2 = mob_->entityWidth() * mob_->entityWidth();
    const double refX = mob_->posX();
    const double refZ = mob_->posZ();
    const double refY = (double)getSurfaceY();

    // Era loop 1: the scan ends at the first cell that changes height.
    int32_t scanEnd = path_->getSize();
    for (int32_t i = path_->getIndex(); i < path_->getSize(); ++i) {
        const PathNode* n = path_->get(i);
        if (n && n->yCoord != (int32_t)refY) {
            scanEnd = i;
            break;
        }
    }

    // Era loop 2: skip the cells the body already covers.
    for (int32_t j = path_->getIndex(); j < scanEnd; ++j) {
        const Path::WalkPos p = path_->getPos(mob_->entityWidth(), j);
        const double dx = p.x - refX;
        const double dy = p.y - refY;
        const double dz = p.z - refZ;
        if (dx * dx + dy * dy + dz * dz < width2)
            path_->setIndex(j + 1);
    }

    // Era straight-line shortcut: jump the index to the farthest cell a
    // clear walkable segment reaches.
    for (int32_t j = scanEnd - 1; j >= path_->getIndex(); --j) {
        const PathNode* n = path_->get(j);
        if (!n)
            continue;
        if (canMoveDirectly(refX, refY, refZ, (double)n->xCoord,
                            (double)n->yCoord, (double)n->zCoord,
                            (int32_t)std::ceil(mob_->entityWidth()),
                            (int32_t)mob_->entityHeight() + 1,
                            (int32_t)std::ceil(mob_->entityWidth()))) {
            path_->setIndex(j);
            break;
        }
    }

    // Era stuck check: 100 ticks without 1.5 blocks of progress abandons
    // the path.
    if (age_ticks_ - last_progress_age_ > 100) {
        const double dx = last_progress_x_ - refX;
        const double dy = last_progress_y_ - refY;
        const double dz = last_progress_z_ - refZ;
        if (dx * dx + dy * dy + dz * dz < 2.25) {
            stop();
            return;
        }
        last_progress_age_ = age_ticks_;
        last_progress_x_ = refX;
        last_progress_y_ = refY;
        last_progress_z_ = refZ;
    }
}

void PathNavigation::trimPathFromSun() {
    if (!path_ || path_->getSize() == 0 || !mob_)
        return;
    const int32_t x = math::floorToInt(mob_->posX());
    const int32_t z = math::floorToInt(mob_->posZ());
    // Only a path that starts under cover gets trimmed (era guard).
    if (skyOpen(x, math::floorToInt(mob_->posY() + 0.5), z))
        return;
    for (int32_t i = 0; i < path_->getSize(); ++i) {
        const PathNode* n = path_->get(i);
        if (n && skyOpen(n->xCoord, n->yCoord, n->zCoord)) {
            path_->setSize(i - 1);
            return;
        }
    }
}

bool PathNavigation::skyOpen(int32_t x, int32_t y, int32_t z) const {
    // A bounded column scan: no solid cell above the probe position.
    for (int32_t by = y + 1; by <= y + 80; ++by) {
        const uint8_t id = level_->getBlockId(x, by, z);
        if (id != 0 && World::entityCollidable(id))
            return false;
    }
    return true;
}

bool PathNavigation::canMoveDirectly(double sx, double sy, double sz,
                                     double ex, double ey, double ez,
                                     int32_t w, int32_t h, int32_t d) const {
    // Era canMoveDirectly: a voxel walk along the segment; every crossed
    // column stays walkable for the body.
    const double dx = ex - sx;
    const double dz = ez - sz;
    if (dx * dx + dz * dz < 0.000001)
        return false;
    const double len = std::sqrt(dx * dx + dz * dz);
    const double ux = dx / len;
    const double uz = dz / len;

    const int32_t refY = (int32_t)sy;
    if (!canWalkOn((int32_t)std::floor(sx), refY, (int32_t)std::floor(sz),
                   w + 2, h, d + 2, sx, sz, ux, uz))
        return false;

    int32_t cx = (int32_t)std::floor(sx);
    int32_t cz = (int32_t)std::floor(sz);
    const int32_t gx = (int32_t)ex;
    const int32_t gz = (int32_t)ez;
    double tNextX = (ux >= 0.0 ? (double)(cx + 1) - sx : sx - (double)cx) /
                    std::fabs(ux);
    double tNextZ = (uz >= 0.0 ? (double)(cz + 1) - sz : sz - (double)cz) /
                    std::fabs(uz);
    const int32_t stepX = ux >= 0.0 ? 1 : -1;
    const int32_t stepZ = uz >= 0.0 ? 1 : -1;
    while (stepX * (gx - cx) > 0 || stepZ * (gz - cz) > 0) {
        if (tNextX < tNextZ) {
            cx += stepX;
            tNextX += 1.0 / std::fabs(ux);
        } else {
            cz += stepZ;
            tNextZ += 1.0 / std::fabs(uz);
        }
        if (!canWalkOn(cx, refY, cz, w, h, d, sx, sz, ux, uz))
            return false;
    }
    return true;
}

bool PathNavigation::canWalkOn(int32_t x, int32_t y, int32_t z, int32_t w,
                               int32_t h, int32_t d, double ox, double oz,
                               double ux, double uz) const {
    // Era canWalkOn/canWalkAbove pair: the column box is non-solid and
    // its floor solid, for the cells on the forward side of the axis.
    const int32_t x0 = x - w / 2;
    const int32_t z0 = z - d / 2;
    for (int32_t bx = x0; bx < x0 + w; ++bx) {
        for (int32_t bz = z0; bz < z0 + d; ++bz) {
            const double dot = ((double)bz + 0.5 - oz) * uz +
                               ((double)bx + 0.5 - ox) * ux;
            if (dot < 0.0)
                continue;
            for (int32_t by = y; by < y + h; ++by) {
                const uint8_t id = level_->getBlockId(bx, by, bz);
                if (id != 0 && World::entityCollidable(id))
                    return false;
            }
            // The floor cell below the box must be solid (water only
            // floors a swimming body).
            const uint8_t floorId = level_->getBlockId(bx, y - 1, bz);
            if (floorId == 0)
                return false;
            if (source_.isWater(floorId) && !mob_->isInWater())
                return false;
            if (source_.isLava(floorId))
                return false;
        }
    }
    return true;
}

} // namespace cursestone::entity::ai
