/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
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
// LiquidFlow.cpp — vanilla MCPE 1.0.0 liquid flow, ported function by
// function from handheld/src/common/world/level/block/LiquidBlockDynamic.cpp
// and LiquidBlockStatic.cpp. Meta 0 = source, 1-7 = flowing level,
// >= 8 = falling column; static/dynamic ids are 9/8 (water) and 11/10 (lava).
#include "LiquidFlow.h"
#include "Block.h"
#include "BlockIds.h"
#include "world/World.h"
#include "scheduler/ServerScheduler.h"
#include "scheduler/CallbackTask.h"
#include <array>
#include <memory>

namespace cursestone::block {

namespace {

// Vanilla facing order used by BlockSource::updateNeighborsAt: W, E, S, N, U, D.
constexpr int kNeighborOffsets[6][3] = {{-1, 0, 0}, {1, 0, 0}, {0, 0, 1},
                                        {0, 0, -1}, {0, 1, 0}, {0, -1, 0}};
// LiquidBlock::onPlace checks S, N, E, W, above.
constexpr int kPlaceCheckOffsets[5][3] = {{0, 0, 1}, {0, 0, -1}, {1, 0, 0},
                                          {-1, 0, 0}, {0, 1, 0}};
// _getSpread direction table: 0 = west, 1 = east, 2 = north, 3 = south.
constexpr int kSpreadDx[4] = {-1, 1, 0, 0};
constexpr int kSpreadDz[4] = {0, 0, -1, 1};

bool sameLiquid(uint8_t a, uint8_t b) {
    return (LiquidFlow::isWaterId(a) && LiquidFlow::isWaterId(b)) ||
           (LiquidFlow::isLavaId(a) && LiquidFlow::isLavaId(b));
}

// Reads the LiquidDepth meta, -1 when the cell is not this liquid family.
int getDepth(world::World* w, int32_t x, int32_t y, int32_t z, uint8_t selfId) {
    if (y < 0 || y >= 256)
        return -1;
    uint8_t id = w->getBlockId(x, y, z);
    if (!sameLiquid(id, selfId))
        return -1;
    return w->getBlockData(x, y, z);
}

// Vanilla writes are chunk writes plus a client packet; World::setBlock keeps
// relight/heightmap/falling checks and skips the liquid hook via notify=false.
void writeLiquid(world::World* w, int32_t x, int32_t y, int32_t z, uint8_t id, uint8_t data) {
    if (y < 0 || y >= 256)
        return;
    w->setBlock(x, y, z, id, data, false);
    sendUpdateBlock(w, x, y, z, id, data);
}

// Vanilla setBlockAndData(..., UPDATE_ALL): write then notify the neighbors.
void writeLiquidNotify(world::World* w, int32_t x, int32_t y, int32_t z, uint8_t id, uint8_t data) {
    writeLiquid(w, x, y, z, id, data);
    LiquidFlow::onBlockChanged(w, x, y, z);
}

// Vanilla Block::isWaterBlocking: blocksMotion/solid materials block, liquids
// and thin floor covers (rails, wire, snow layer, carpets, plants) pass.
bool isThinCover(uint8_t id) {
    switch (id) {
    case SAPLING:
    case COBWEB:
    case TALL_GRASS:
    case DEAD_BUSH:
    case FLOWER_YELLOW:
    case FLOWER_RED:
    case BROWN_MUSHROOM:
    case RED_MUSHROOM:
    case TORCH:
    case REDSTONE_WIRE:
    case SIGN_POST:
    case WALL_SIGN:
    case LADDER:
    case RAIL:
    case POWERED_RAIL:
    case DETECTOR_RAIL:
    case LEVER:
    case STONE_PRESSURE_PLATE:
    case WOOD_PRESSURE_PLATE:
    case STONE_BUTTON:
    case SNOW_LAYER:
    case WHITE_CARPET:
    case SUGARCANE:
    case PUMPKIN_STEM:
    case MELON_STEM:
    case VINE:
    case WATER_LILY:
    case CARROT:
    case POTATO:
    case BEETROOT:
        return true;
    default:
        return false;
    }
}

uint32_t nextRandom() {
    // xorshift32 stand-in for the world Random used by LiquidBlockDynamic.
    static uint32_t state = 0x2545F491u;
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

// Vanilla LiquidBlockDynamic::_getHighest.
int getHighest(world::World* w, int32_t x, int32_t y, int32_t z, uint8_t selfId, int current,
               int& outMaxCount) {
    int d = getDepth(w, x, y, z, selfId);
    if (d < 0)
        return current;
    if (d == 0)
        outMaxCount++;
    if (d >= 8)
        d = 0;
    return (current < 0 || d < current) ? d : current;
}

bool canSpreadTo(world::World* w, int32_t x, int32_t y, int32_t z, uint8_t selfId);

// Vanilla LiquidBlockDynamic::_getSlopeDistance.
int getSlopeDistance(world::World* w, int32_t x, int32_t y, int32_t z, int pass, int from,
                     uint8_t selfId) {
    int lowest = 1000;
    for (int d = 0; d < 4; d++) {
        if ((d == 0 && from == 1) || (d == 1 && from == 0) || (d == 2 && from == 3) ||
            (d == 3 && from == 2))
            continue;
        int32_t nx = x + kSpreadDx[d], nz = z + kSpreadDz[d];
        if (!w->isChunkLoaded(nx, nz))
            continue;
        uint8_t nid = w->getBlockId(nx, y, nz);
        if (LiquidFlow::isWaterBlockingId(nid))
            continue;
        if (sameLiquid(nid, selfId)) {
            if (w->getBlockData(nx, y, nz) == 0)
                continue;
        } else {
            if (y - 1 < 0 || !LiquidFlow::isWaterBlockingId(w->getBlockId(nx, y - 1, nz)))
                return pass;
            if (pass < 4) {
                int v = getSlopeDistance(w, nx, y, nz, pass + 1, d, selfId);
                if (v < lowest)
                    lowest = v;
            }
        }
    }
    return lowest;
}

// Vanilla LiquidBlockDynamic::_getSpread.
std::array<bool, 4> getSpread(world::World* w, int32_t x, int32_t y, int32_t z, uint8_t selfId) {
    std::array<int, 4> dist{1000, 1000, 1000, 1000};
    for (int d = 0; d < 4; d++) {
        int32_t nx = x + kSpreadDx[d], nz = z + kSpreadDz[d];
        if (!w->isChunkLoaded(nx, nz))
            continue;
        uint8_t nid = w->getBlockId(nx, y, nz);
        if (LiquidFlow::isWaterBlockingId(nid))
            continue;
        if (sameLiquid(nid, selfId)) {
            if (w->getBlockData(nx, y, nz) == 0)
                continue;
        } else {
            if (y - 1 < 0 || !LiquidFlow::isWaterBlockingId(w->getBlockId(nx, y - 1, nz)))
                dist[d] = 0;
            else
                dist[d] = getSlopeDistance(w, nx, y, nz, 1, d, selfId);
        }
    }
    int lowest = dist[0];
    for (int d = 1; d < 4; d++)
        if (dist[d] < lowest)
            lowest = dist[d];
    std::array<bool, 4> result{};
    for (int d = 0; d < 4; d++)
        result[d] = dist[d] == lowest;
    return result;
}

// Vanilla LiquidBlockDynamic::_canSpreadTo.
bool canSpreadTo(world::World* w, int32_t x, int32_t y, int32_t z, uint8_t selfId) {
    if (y < 0 || y >= 256)
        return false;
    if (!w->isChunkLoaded(x, z))
        return false;
    uint8_t id = w->getBlockId(x, y, z);
    if (sameLiquid(id, selfId))
        return false;
    if (LiquidFlow::isLavaId(id))
        return false;
    return !LiquidFlow::isWaterBlockingId(id);
}

// Vanilla LiquidBlockDynamic::_spread + _trySpreadTo.
void trySpreadTo(world::World* w, int32_t x, int32_t y, int32_t z, uint8_t selfId,
                 int neighborDepth) {
    if (!canSpreadTo(w, x, y, z, selfId))
        return;
    writeLiquidNotify(w, x, y, z, selfId, (uint8_t)neighborDepth);
    LiquidFlow::scheduleTick(w, x, y, z);
}

// Vanilla LiquidBlock::solidify — lava turns obsidian/cobble next to water.
bool solidify(world::World* w, int32_t x, int32_t y, int32_t z, int32_t nx, int32_t ny,
              int32_t nz) {
    if (ny - y < 0)
        return false;
    uint8_t selfId = w->getBlockId(x, y, z);
    if (!LiquidFlow::isLavaId(selfId) || !LiquidFlow::isWaterId(w->getBlockId(nx, ny, nz)))
        return false;
    int depth = w->getBlockData(x, y, z);
    if (depth == 0) {
        writeLiquidNotify(w, x, y, z, OBSIDIAN, 0);
        return true;
    }
    if (depth <= 4) {
        writeLiquidNotify(w, x, y, z, COBBLESTONE, 0);
        return true;
    }
    return false;
}

} // namespace

bool LiquidFlow::isLiquidId(uint8_t id) {
    return id >= FLOWING_WATER && id <= LAVA;
}

bool LiquidFlow::isDynamicLiquidId(uint8_t id) {
    return id == FLOWING_WATER || id == FLOWING_LAVA;
}

bool LiquidFlow::isStillLiquidId(uint8_t id) {
    return id == WATER || id == LAVA;
}

bool LiquidFlow::isWaterId(uint8_t id) {
    return id == FLOWING_WATER || id == WATER;
}

bool LiquidFlow::isLavaId(uint8_t id) {
    return id == FLOWING_LAVA || id == LAVA;
}

bool LiquidFlow::isWaterBlockingId(uint8_t id) {
    if (id == AIR || isLiquidId(id))
        return false;
    return !isThinCover(id);
}

bool LiquidFlow::isReplaceableId(uint8_t id) {
    if (id == AIR)
        return true;
    Block* b = Block::get(id);
    return b && b->canBeReplaced();
}

int LiquidFlow::tickDelayFor(world::World* world, uint8_t id) {
    if (isWaterId(id))
        return 5;
    if (isLavaId(id))
        return (world && world->dimension() == 1) ? 10 : 30;
    return 0;
}

void LiquidFlow::onLiquidPlaced(world::World* world, int32_t x, int32_t y, int32_t z) {
    if (!world)
        return;
    uint8_t selfId = world->getBlockId(x, y, z);
    if (!isDynamicLiquidId(selfId))
        return;
    for (const auto& o : kPlaceCheckOffsets) {
        int32_t nx = x + o[0], ny = y + o[1], nz = z + o[2];
        if (ny < 0 || ny >= 256)
            continue;
        if (!world->isChunkLoaded(nx, nz))
            continue;
        if (solidify(world, x, y, z, nx, ny, nz))
            return;
    }
    // LiquidBlockDynamic::onPlace re-queues the cell so a bucket pour flows.
    scheduleTick(world, x, y, z);
}

void LiquidFlow::onBlockChanged(world::World* world, int32_t x, int32_t y, int32_t z) {
    if (!world || y < 0 || y >= 256)
        return;
    if (!world->isChunkLoaded(x, z))
        return;
    uint8_t centerId = world->getBlockId(x, y, z);

    // Vanilla setBlockAndData(UPDATE_ALL) fires onPlace on the new block.
    if (isDynamicLiquidId(centerId))
        onLiquidPlaced(world, x, y, z);

    // Vanilla updateNeighborsAt: each liquid neighbor runs neighborChanged.
    for (const auto& o : kNeighborOffsets) {
        int32_t nx = x + o[0], ny = y + o[1], nz = z + o[2];
        if (ny < 0 || ny >= 256)
            continue;
        if (!world->isChunkLoaded(nx, nz))
            continue;
        uint8_t nid = world->getBlockId(nx, ny, nz);
        if (!isLiquidId(nid))
            continue;
        if (isLavaId(nid) && isWaterId(centerId) && ny <= y) {
            if (solidify(world, nx, ny, nz, x, y, z))
                continue;
        }
        if (isStillLiquidId(nid)) {
            // LiquidBlockStatic::_setDynamic (UPDATE_CLIENTS, no re-notify).
            uint8_t nd = world->getBlockData(nx, ny, nz);
            writeLiquid(world, nx, ny, nz, (uint8_t)(nid - 1), nd);
            scheduleTick(world, nx, ny, nz);
        }
    }
}

bool LiquidFlow::wakeSettledLiquid(world::World* world, int32_t x, int32_t y, int32_t z) {
    if (!world || y < 0 || y >= 256)
        return false;
    uint8_t id = world->getBlockId(x, y, z);
    if (!isStillLiquidId(id))
        return false;
    // Only cells with somewhere to go: one of the four sides or the cell
    // below must accept the flow. Open sky alone is not enough, so ocean
    // and river surfaces stay at rest instead of churning every load.
    static constexpr int kOff[5][3] = {{-1, 0, 0}, {1, 0, 0}, {0, 0, -1}, {0, 0, 1}, {0, -1, 0}};
    for (const auto& o : kOff) {
        if (!canSpreadTo(world, x + o[0], y + o[1], z + o[2], id))
            continue;
        uint8_t data = world->getBlockData(x, y, z);
        // LiquidBlockStatic::_setDynamic (UPDATE_CLIENTS, no re-notify).
        writeLiquid(world, x, y, z, (uint8_t)(id - 1), data);
        scheduleTick(world, x, y, z);
        return true;
    }
    return false;
}

void LiquidFlow::scheduleTick(world::World* world, int32_t x, int32_t y, int32_t z) {
    if (!world)
        return;
    uint8_t id = world->getBlockId(x, y, z);
    if (!isDynamicLiquidId(id))
        return;
    int delay = tickDelayFor(world, id);
    scheduler::ServerScheduler::get().scheduleSync(
        std::make_shared<scheduler::CallbackTask>(
            [world, x, y, z] { LiquidFlow::onScheduledTick(world, x, y, z); }),
        delay);
}

void LiquidFlow::onScheduledTick(world::World* world, int32_t x, int32_t y, int32_t z) {
    if (!world || y < 0 || y >= 256)
        return;
    if (!world->isChunkLoaded(x, z))
        return;
    uint8_t id = world->getBlockId(x, y, z);
    if (!isDynamicLiquidId(id))
        return;
    bool lava = isLavaId(id);

    int depth = world->getBlockData(x, y, z);
    int dropOff = (lava && world->dimension() != 1) ? 2 : 1;
    int tickDelay = tickDelayFor(world, id);
    // Vanilla trySpreadFire is skipped: fire spread is disabled on this server.

    if (depth > 0) {
        int highest = -100;
        int maxCount = 0;
        highest = getHighest(world, x - 1, y, z, id, highest, maxCount);
        highest = getHighest(world, x + 1, y, z, id, highest, maxCount);
        highest = getHighest(world, x, y, z + 1, id, highest, maxCount);
        highest = getHighest(world, x, y, z - 1, id, highest, maxCount);

        int newDepth = highest + dropOff;
        if (newDepth >= 8 || highest < 0)
            newDepth = -1;

        int aboveDepth = getDepth(world, x, y + 1, z, id);
        if (aboveDepth >= 0)
            newDepth = (aboveDepth >= 8) ? aboveDepth : aboveDepth + 8;

        if (maxCount >= 2 && !lava) {
            // Only spread a spring on top of solid ground or an existing source.
            uint8_t belowId = (y > 0) ? world->getBlockId(x, y - 1, z) : (uint8_t)BEDROCK;
            if (isWaterBlockingId(belowId)) {
                newDepth = 0;
            } else if (sameLiquid(belowId, id) && world->getBlockData(x, y - 1, z) == 0) {
                newDepth = 0;
            }
        }

        if (lava && depth < 8 && newDepth < 8 && newDepth > depth && nextRandom() % 4 != 0)
            tickDelay *= 4;

        if (newDepth != depth) {
            depth = newDepth;
            if (depth < 0) {
                writeLiquidNotify(world, x, y, z, AIR, 0);
            } else {
                writeLiquidNotify(world, x, y, z, id, (uint8_t)depth);
                scheduleTick(world, x, y, z);
            }
        } else {
            writeLiquid(world, x, y, z, (uint8_t)(id + 1), (uint8_t)depth);
        }
    } else {
        writeLiquid(world, x, y, z, (uint8_t)(id + 1), (uint8_t)depth);
    }

    // Spread phase: down when possible, otherwise along the best slope.
    bool spreadDown = false;
    if (y > 0) {
        uint8_t belowId = world->getBlockId(x, y - 1, z);
        if (lava && isWaterId(belowId)) {
            writeLiquidNotify(world, x, y - 1, z, STONE, 0);
            return;
        }
        if (canSpreadTo(world, x, y - 1, z, id)) {
            trySpreadTo(world, x, y - 1, z, id, depth >= 8 ? depth : depth + 8);
            spreadDown = true;
        }
    }
    if (spreadDown)
        return;
    if (depth >= 0 && (depth == 0 || (y > 0 && isWaterBlockingId(world->getBlockId(x, y - 1, z))))) {
        std::array<bool, 4> spreads = getSpread(world, x, y, z, id);
        int neighbor = depth + dropOff;
        if (depth >= 8)
            neighbor = 1;
        if (neighbor >= 8)
            return;
        if (spreads[0])
            trySpreadTo(world, x + kSpreadDx[0], y, z + kSpreadDz[0], id, neighbor);
        if (spreads[1])
            trySpreadTo(world, x + kSpreadDx[1], y, z + kSpreadDz[1], id, neighbor);
        if (spreads[2])
            trySpreadTo(world, x + kSpreadDx[2], y, z + kSpreadDz[2], id, neighbor);
        if (spreads[3])
            trySpreadTo(world, x + kSpreadDx[3], y, z + kSpreadDz[3], id, neighbor);
    }
}

} // namespace cursestone::block
