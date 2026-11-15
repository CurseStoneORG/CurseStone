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
// SimpleLoot — weighted loot pools for generated chests.
//
// Structure pieces (vgen) name a table the vanilla way
// ("loot_tables/chests/simple_dungeon.json"); findTable() maps the known
// names onto a weighted pool and rollTable() draws `rolls` stacks from it.
// The result is filled into the chest's tile NBT at generation time, so a
// generated chest carries its loot from the very first hydration and the
// roll is never repeated (once the tile saves, the blob is authoritative).
//
// TODO: add more loot — richer per-table pools, enchanted gear, rare
// drops and per-biome variants land on top of these tables.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "item/ItemStack.h"

namespace cursestone::loot {

// One weighted entry of a pool: item id, aux value and the count range a
// roll may draw from the entry. `weight` is relative inside its pool.
struct LootEntry {
    uint16_t id;
    uint8_t meta;
    uint8_t minCount;
    uint8_t maxCount;
    uint32_t weight;
};

// A pool: how many entries one chest roll draws and what it draws from.
struct LootTable {
    uint8_t minRolls;
    uint8_t maxRolls;
    std::vector<LootEntry> entries;
};

// Maps a vanilla-style table name onto its pool. Returns null for names
// CurseStone does not ship (callers keep the chest empty then).
const LootTable* findTable(const std::string& table);

// Draws `minRolls..maxRolls` entries from `table` (independent weighted
// picks, counts inclusive-random). `seed` picks the whole roll, so the
// same seed reproduces the same chest — generation hands a per-chest seed
// so population stays reproducible. Empty vector when the table is
// unknown or has no entries.
std::vector<cursestone::item::ItemStack> rollTable(const std::string& table, uint64_t seed);

} // namespace cursestone::loot
