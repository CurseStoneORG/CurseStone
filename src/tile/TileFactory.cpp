/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

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
#include "TileFactory.h"
#include "TileChest.h"
#include "TileFurnace.h"
#include "TileSign.h"
#include "TileSpawner.h"
#include "block/BlockIds.h"
#include "nbt/NBT.h"
#include "nbt/tag/CompoundTag.h"
#include "nbt/tag/Tag.h"
#include "utils/CRaii.h"

namespace cursestone::tile {

bool isTileBlock(uint8_t blockId) {
    using cursestone::block::BlockId;
    return blockId == (uint8_t)BlockId::CHEST || blockId == (uint8_t)BlockId::FURNACE ||
           blockId == (uint8_t)BlockId::BURNING_FURNACE || blockId == (uint8_t)BlockId::SIGN_POST ||
           blockId == (uint8_t)BlockId::WALL_SIGN || blockId == (uint8_t)BlockId::MOB_SPAWNER;
}

uint8_t blockIdForTileId(const std::string& tileId) {
    using cursestone::block::BlockId;
    if (tileId == "Chest")
        return (uint8_t)BlockId::CHEST;
    if (tileId == "Furnace")
        return (uint8_t)BlockId::FURNACE;
    if (tileId == "Sign")
        return (uint8_t)BlockId::SIGN_POST;
    if (tileId == "MobSpawner")
        return (uint8_t)BlockId::MOB_SPAWNER;
    return 0;
}

bool parseTileBlobHeader(const uint8_t* data, size_t len, std::string& tileId,
                         int32_t& x, int32_t& y, int32_t& z) {
    tileId.clear();
    x = y = z = 0;
    if (!data || len == 0)
        return false;
    utils::TagGuard tag(cursestone::nbt::read(data, len, /*little_endian=*/true));
    if (!tag)
        return false;
    const char* id = cursestone::nbt::get_string(tag.get(), "id", "");
    if (!id || id[0] == '\0')
        return false;
    tileId = id;
    x = cursestone::nbt::get_int(tag.get(), "x", 0);
    y = cursestone::nbt::get_int(tag.get(), "y", 0);
    z = cursestone::nbt::get_int(tag.get(), "z", 0);
    return true;
}

bool tileNeedsActorAnnounce(const Tile* t) {
    return t && (dynamic_cast<const TileChest*>(t) || dynamic_cast<const TileFurnace*>(t));
}

std::shared_ptr<Tile> createTileForBlock(uint8_t blockId, const cursestone::world::Position& pos) {
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