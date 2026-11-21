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
// TileFactory — which block entity a legacy block id implies.
//
// The reference server builds these on the fly in Tile::createTile() from the same kind
// of switch (the reference server/tile/Tile + level/format/io
// chunk init). We need it in one place because a chest or furnace that
// arrives with the WORLD (a village piece, a dungeon, a structure) has
// no tile entity behind it until somebody asks for one — and in
// MCPE 0.15 the client renders and opens chests/furnaces through their
// block actor, so a chest block without one shows up as an unknown
// (invisible) block that still collides, and tapping it either does
// nothing or, for a furnace, kicks the client.
//
// World::ensureTilesLoaded() uses this to build the missing tiles the
// moment a chunk is first touched, so they travel with the chunk in the
// BlockEntityDataPacket the player already receives.
#pragma once
#include "Tile.h"
#include <cstdint>
#include <memory>
#include <string>

namespace cursestone::tile {

// The tile a block id implies, or nullptr when the block carries no
// block entity (air, a plain block, a block kind we have no tile for).
// `pos` is the world position of the block.
std::shared_ptr<Tile> createTileForBlock(uint8_t blockId, const cursestone::world::Position& pos);

// True for the block ids createTileForBlock() can build. Used by the
// chunk scan to skip the switch for every block in the world.
bool isTileBlock(uint8_t blockId);

// The block id a tile compound's "id" string ("Chest", "Furnace", ...)
// must stand on, or 0 when the tile id has no block counterpart. The
// inverse of createTileForBlock(): the block↔tile pair is the client's
// render contract (the 0.15 chest model is drawn by the block actor,
// which only binds over a chest block), so every seam that moves a tile
// NBT blob (hydration, structure generation, spill drains) checks the
// pair with this before letting the tile through.
uint8_t blockIdForTileId(const std::string& tileId);

// Reads the identity header of a serialized tile compound (the
// little-endian blob format tile_entities carries and structure
// generation queues): id/x/y/z. Returns false when the blob is not a
// parseable tile compound. Cheap: one compound walk of the three int
// tags plus the id string, no state built.
bool parseTileBlobHeader(const uint8_t* data, size_t len, std::string& tileId,
                         int32_t& x, int32_t& y, int32_t& z);

} // namespace cursestone::tile