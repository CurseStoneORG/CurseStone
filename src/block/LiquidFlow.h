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
// LiquidFlow — 1:1 port of the vanilla MCPE 1.0.0 liquid engine
// (world/level/block/LiquidBlockDynamic + LiquidBlockStatic).
#pragma once
#include <cstdint>

namespace cursestone {
namespace world {
class World;
}
namespace block {

class LiquidFlow {
  public:
    // Vanilla BlockSource::_blockChanged(UPDATE_ALL): onPlace pass for freshly
    // placed dynamic liquid, then neighborChanged on the six neighbors.
    static void onBlockChanged(world::World* world, int32_t x, int32_t y, int32_t z);

    // Vanilla LiquidBlockDynamic::tick.
    static void onScheduledTick(world::World* world, int32_t x, int32_t y, int32_t z);

    // Vanilla BlockTickingQueue::add for a liquid cell.
    static void scheduleTick(world::World* world, int32_t x, int32_t y, int32_t z);

    // Vanilla LiquidBlock::onPlace — lava solidifies against water neighbors.
    static void onLiquidPlaced(world::World* world, int32_t x, int32_t y, int32_t z);

    static bool isLiquidId(uint8_t id);
    static bool isDynamicLiquidId(uint8_t id);
    static bool isStillLiquidId(uint8_t id);
    static bool isWaterId(uint8_t id);
    static bool isLavaId(uint8_t id);
    static bool isWaterBlockingId(uint8_t id);
    static bool isReplaceableId(uint8_t id);
    static int tickDelayFor(world::World* world, uint8_t id);
};

} // namespace block
} // namespace cursestone
