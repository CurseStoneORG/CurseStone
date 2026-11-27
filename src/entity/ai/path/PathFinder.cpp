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
#include "entity/ai/path/PathFinder.h"
#include "entity/ai/AiMath.h"
#include "CurseStone.h"
#include "block/BlockIds.h"
#include "entity/ai/Mob.h"
#include "world/World.h"
#include <algorithm>
#include <cmath>

namespace cursestone::entity::ai {

// ---- WorldPathSource ----

uint8_t WorldPathSource::tileAt(int32_t x, int32_t y, int32_t z) const {
    return world_->getBlockId(x, y, z);
}

bool WorldPathSource::isPathfindable(uint8_t id) const {
    // The server's collision table is the walker's blocker table: a
    // cell the body cannot share is not pathfindable. Non-solid cells
    // (air, grass, water, torches...) are walkable.
    return !World::entityCollidable(id);
}

bool WorldPathSource::isWater(uint8_t id) const {
    return id == block::WATER || id == block::FLOWING_WATER;
}

bool WorldPathSource::isLava(uint8_t id) const {
    return id == block::LAVA || id == block::FLOWING_LAVA;
}

bool WorldPathSource::isWoodDoor(uint8_t id) const {
    return id == block::WOODEN_DOOR;
}

bool WorldPathSource::isFenceOrGate(uint8_t id) const {
    return id == block::FENCE || id == block::FENCE_GATE;
}

bool WorldPathSource::isTrapdoor(uint8_t id) const {
    return id == block::TRAPDOOR;
}

// ---- PathFinder ----

PathNode* PathFinder::getNode(int32_t x, int32_t y, int32_t z) {
    // Era Node::createHash keyed the cache; the port packs the full
    // cell into 64 bits so lookups never collide (each field keeps 21
    // bits, enough for every reachable coordinate).
    const uint64_t key =
        ((uint64_t)(uint32_t)(x & 0x1FFFFF) << 42) |
        ((uint64_t)(uint32_t)(y & 0x1FFFFF) << 21) |
        (uint64_t)(uint32_t)(z & 0x1FFFFF);
    const auto it = nodes_.find(key);
    if (it != nodes_.end())
        return it->second.get();
    PathNode* node = new PathNode(x, y, z);
    nodes_.emplace(key, std::unique_ptr<PathNode>(node));
    return node;
}

int32_t PathFinder::isFree(const Mob& mob, int32_t x, int32_t y, int32_t z,
                           const PathBox& box) const {
    bool swim_node = false;
    for (int32_t bx = x; bx < x + box.x; ++bx) {
        for (int32_t by = y; by < y + box.y; ++by) {
            for (int32_t bz = z; bz < z + box.z; ++bz) {
                const uint8_t id = source_->tileAt(bx, by, bz);
                if (id == 0)
                    continue;
                if (source_->isWater(id)) {
                    if (avoid_water_)
                        return -1;
                    swim_node = true;
                }
                if (!source_->isPathfindable(id) &&
                    !(can_open_doors_ && source_->isWoodDoor(id))) {
                    if (source_->isFenceOrGate(id))
                        return -3;
                    if (source_->isTrapdoor(id))
                        return -4;
                    if (!source_->isLava(id))
                        return 0;
                    // Lava only blocks a body that is not already in it.
                    if (!mob.isInLava())
                        return -2;
                }
            }
        }
    }
    return swim_node ? 2 : 1;
}

PathNode* PathFinder::getNodeClimb(const Mob& mob, int32_t x, int32_t y,
                                   int32_t z, const PathBox& box,
                                   int32_t climb) {
    // Era getNode(entity, x, y, z, box, climb).
    int32_t ny = y;
    const int32_t free = isFree(mob, x, ny, z, box);
    if (free == 2)
        return getNode(x, ny, z); // Swim node: no ascent check.
    if (free == 1) {
        return ascendCheck(mob, getNode(x, ny, z), ny, x, z, box);
    }
    if (climb <= 0 || free == -3 || free == -4)
        return nullptr;
    // Step up: the climb cell itself must be walkable.
    ny += climb;
    if (isFree(mob, x, ny, z, box) != 1)
        return nullptr;
    return ascendCheck(mob, getNode(x, ny, z), ny, x, z, box);
}

PathNode* PathFinder::ascendCheck(const Mob& mob, PathNode* node,
                                  int32_t y, int32_t x, int32_t z,
                                  const PathBox& box) {
    // Era ascent: a candidate is only accepted when it stands on walkable
    // ground within four cells below (never over water when avoidWater
    // is set, never over lava). The scan stops at the first non-walkable
    // cell below (the floor).
    int32_t ny = y;
    int32_t tries = 4;
    while (ny > 0) {
        const int32_t below = isFree(mob, x, ny - 1, z, box);
        if (avoid_water_ && below == -1)
            return nullptr;
        if (below != 1)
            return below == -2 ? nullptr : node;
        if (--tries == 0)
            return nullptr;
        if (--ny == 0)
            return node;
        node = getNode(x, ny, z);
    }
    return node;
}

std::unique_ptr<Path> PathFinder::findPath(const Mob& mob, double tx,
                                           double ty, double tz,
                                           float range) {
    nodes_.clear();
    heap_.clear();

    // Era start cell: floor(minY + 0.5), or the water surface when the
    // mob swims and canFloat (bounded scan).
    int32_t startY;
    const int32_t sx = math::floorToInt(mob.posX());
    const int32_t sz = math::floorToInt(mob.posZ());
    if (can_float_ && mob.isInWater()) {
        startY = (int32_t)mob.posY();
        const int32_t maxScan = startY + 17;
        uint8_t id = source_->tileAt(sx, startY, sz);
        while (source_->isWater(id) && startY < maxScan) {
            ++startY;
            id = source_->tileAt(sx, startY, sz);
        }
    } else {
        startY = math::floorToInt(mob.posY() + 0.5);
    }
    PathNode* start = getNode(sx, startY, sz);

    // Era goal cell: the target cell when its floor is solid, else the
    // first walkable cell of the body-width box, else the first ground
    // below the direct cell.
    const double halfW = (double)mob.entityWidth() * 0.5;
    int32_t gx = math::floorToInt(tx - halfW);
    int32_t gy = (int32_t)ty;
    int32_t gz = math::floorToInt(tz - halfW);
    PathNode* goal = nullptr;
    if (source_->tileAt(gx, gy - 1, gz) != 0) {
        goal = getNode(gx, gy, gz);
    } else {
        const int32_t gxMax = math::floorToInt(tx + halfW);
        const int32_t gzMax = math::floorToInt(tz + halfW);
        for (int32_t cx = gx; cx <= gxMax && !goal; ++cx) {
            for (int32_t cz = gz; cz <= gzMax; ++cz) {
                if (source_->tileAt(cx, gy - 1, cz) != 0) {
                    goal = getNode(cx, gy, cz);
                    break;
                }
            }
        }
        if (!goal) {
            while (source_->tileAt(gx, gy - 1, gz) == 0 && gy > 0)
                --gy;
            goal = getNode(gx, gy, gz);
        }
    }

    // Era body box: (int)(width + 1) wide, (int)(height + 1) tall.
    PathBox box;
    box.x = box.z = (int32_t)std::floor(mob.entityWidth() + 1.0);
    box.y = (int32_t)std::floor(mob.entityHeight() + 1.0);

    return findPath(mob, *start, *goal, box, range);
}

std::unique_ptr<Path> PathFinder::findPath(const Mob& mob,
                                           const PathNode& startNode,
                                           const PathNode& goalNode,
                                           const PathBox& box, float range) {
    PathNode* start = getNode(startNode.xCoord, startNode.yCoord,
                              startNode.zCoord);
    PathNode* goal = getNode(goalNode.xCoord, goalNode.yCoord,
                             goalNode.zCoord);
    PathNode* best = start;
    start->g = 0.0f;
    start->h = start->distanceTo(*goal);
    start->f = start->h;
    start->parent = nullptr;
    start->closed = false;
    heap_.clear();
    heap_.insert(start);

    while (!heap_.isEmpty()) {
        PathNode* current = heap_.pop();
        if (current->equals(*goal))
            return reconstruct(start, current);
        const float distToGoal = current->distanceTo(*goal);
        if (distToGoal < best->distanceTo(*goal))
            best = current;
        current->closed = true;

        neighbor_count_ = getNeighbors(mob, *current, *goal, box, range);
        for (int32_t i = 0; i < neighbor_count_; ++i) {
            PathNode* neighbor = getNode(neighbor_nodes_[i].xCoord,
                                         neighbor_nodes_[i].yCoord,
                                         neighbor_nodes_[i].zCoord);
            if (neighbor->closed)
                continue;
            const float candidate =
                current->g + current->distanceTo(neighbor_nodes_[i]);
            const bool inOpen = neighbor->heap_index >= 0;
            if (!inOpen || candidate < neighbor->g) {
                neighbor->parent = current;
                neighbor->g = candidate;
                neighbor->h = neighbor->distanceTo(*goal);
                neighbor->f = candidate + neighbor->h;
                if (inOpen)
                    heap_.changeCost(neighbor);
                else
                    heap_.insert(neighbor);
            }
        }
    }
    if (best != start)
        return reconstruct(start, best);
    return nullptr;
}

int32_t PathFinder::getNeighbors(const Mob& mob, const PathNode& current,
                                 const PathNode& goal, const PathBox& box,
                                 float range) {
    // Era: the climb flag is the freeness of the cell above the current
    // one.
    const int32_t climb =
        isFree(mob, current.xCoord, current.yCoord + 1, current.zCoord, box) == 1
            ? 1
            : 0;
    int32_t count = 0;
    const auto push = [&](PathNode* node) {
        if (!node || node->closed)
            return;
        if (node->distanceTo(goal) >= range)
            return;
        neighbor_nodes_[count++] = *node;
    };
    push(getNodeClimb(mob, current.xCoord, current.yCoord,
                      current.zCoord + 1, box, climb));
    push(getNodeClimb(mob, current.xCoord - 1, current.yCoord,
                      current.zCoord, box, climb));
    push(getNodeClimb(mob, current.xCoord + 1, current.yCoord,
                      current.zCoord, box, climb));
    push(getNodeClimb(mob, current.xCoord, current.yCoord,
                      current.zCoord - 1, box, climb));
    return count;
}

std::unique_ptr<Path> PathFinder::reconstruct(const PathNode* start,
                                              const PathNode* end) const {
    std::vector<PathNode> cells;
    const PathNode* node = end;
    while (node) {
        cells.push_back(*node);
        if (node == start)
            break;
        node = node->parent;
    }
    std::reverse(cells.begin(), cells.end());
    return std::make_unique<Path>(std::move(cells));
}

} // namespace cursestone::entity::ai
