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
// LootChest — the one entry point structure generation uses to place a
// loot chest. Placing the chest BLOCK alone is what produced the
// "invisible chest" reports: in MCPE 0.15 the chest model is drawn by the
// client's block actor, and that actor only exists once the
// BlockEntityDataPacket with the tile compound arrives — a chest block
// without its tile renders as air. So this helper places the block AND
// queues the chest's tile NBT blob (id/x/y/z/Items) onto the chunk that
// owns the position; the blob rides the same chunk stream the save/load
// path already speaks and the tile materializes with its loot on the
// first hydration.
//
// TODO: add more loot — the pools live in src/world/loot/SimpleLoot.cpp.
#pragma once

#include <string>

#include "vgen/VgenWorld.h"

namespace vgen {

// Places a chest block at the WORLD position (x, y, z) with the given
// facing data and queues its loot-filled tile blob for the owning chunk.
// Returns true when the block was placed; the tile blob follows
// regardless of whether the position is in the chunk being populated or
// in a neighbor (same routing the block write takes).
bool placeLootChest(BlockSource& region, int x, int y, int z, DataID facing,
                    const std::string& lootTable, Random& random);

} // namespace vgen
