// SPDX-License-Identifier: LGPL-3.0-or-later
#include "TileFactory.h"
#include "TileChest.h"
#include "TileFurnace.h"
#include "TileSign.h"
#include "TileSpawner.h"
#include "block/BlockIds.h"

namespace cursestone::tile {

bool isTileBlock(uint8_t blockId) {
    using cursestone::block::BlockId;
    return blockId == (uint8_t)BlockId::CHEST ||
           blockId == (uint8_t)BlockId::FURNACE ||
           blockId == (uint8_t)BlockId::BURNING_FURNACE ||
           blockId == (uint8_t)BlockId::SIGN_POST ||
           blockId == (uint8_t)BlockId::WALL_SIGN ||
           blockId == (uint8_t)BlockId::MOB_SPAWNER;
}

std::shared_ptr<Tile> createTileForBlock(
        uint8_t blockId, const cursestone::level::Position& pos) {
    using cursestone::block::BlockId;
    switch (blockId) {
        case (uint8_t)BlockId::CHEST:
            return std::make_shared<TileChest>(pos);
        // A lit furnace (id 62) is the same tile with burn time already
        // running: the block id only says "there is fire in here".
        case (uint8_t)BlockId::FURNACE:
        case (uint8_t)BlockId::BURNING_FURNACE:
            return std::make_shared<TileFurnace>(pos);
        case (uint8_t)BlockId::SIGN_POST:
        case (uint8_t)BlockId::WALL_SIGN:
            return std::make_shared<TileSign>(pos);
        case (uint8_t)BlockId::MOB_SPAWNER:
            return std::make_shared<TileSpawner>(pos);
        default:
            return nullptr;
    }
}

} // namespace cursestone::tile