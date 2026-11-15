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
#include "world/loot/SimpleLoot.h"

#include <cstdint>

#include "block/BlockIds.h"  // block-as-item entries (obsidian)
#include "item/ItemIds.h"    // loot item ids (iron/gold/bread/...)

namespace cursestone::loot {
namespace {

// ---- pools -------------------------------------------------------------
// Simplified vanilla 0.15 tables: same item families and count ranges as
// the era tables, without the enchantment book entries (loot keeps to
// plain stacks until the enchant roll exists).
// TODO: add more loot — enchanted gear, rare drops and per-biome variants.

const LootTable kSimpleDungeon = {
    6, 8,
    {
        {item::ITEM_SADDLE,        0, 1, 1,  3},
        {item::ITEM_GOLDEN_APPLE,  0, 1, 1,  3},
        {item::ITEM_GUNPOWDER,     0, 1, 4, 10},
        {item::ITEM_STRING,        0, 1, 4, 10},
        {item::ITEM_WHEAT,         0, 1, 3, 10},
        {item::ITEM_BREAD,         0, 1, 1, 15},
        {item::ITEM_APPLE,         0, 1, 1, 15},
        {item::ITEM_REDSTONE,      0, 1, 4, 15},
        {item::ITEM_IRON_INGOT,    0, 1, 4, 10},
        {item::ITEM_BUCKET,        0, 1, 1, 10},
    },
};

const LootTable kVillageBlacksmith = {
    3, 8,
    {
        {item::ITEM_IRON_INGOT,    0, 1, 3,  5},
        {item::ITEM_APPLE,         0, 1, 3, 15},
        {item::ITEM_COAL,          0, 1, 3, 10},
        {item::ITEM_IRON_PICKAXE,  0, 1, 1,  5},
        {item::ITEM_DIAMOND,       0, 1, 3,  1},
        {item::ITEM_SADDLE,        0, 1, 1,  1},
    },
};

const LootTable kVillageTwoRoomHouse = {
    3, 8,
    {
        {item::ITEM_BREAD,         0, 1, 4,  5},
        {item::ITEM_WHEAT,         0, 1, 4,  5},
        {item::ITEM_CARROT,        0, 1, 4,  5},
        {item::ITEM_POTATO,        0, 1, 4,  5},
        {item::ITEM_APPLE,         0, 1, 3,  5},
        {item::ITEM_EMERALD,       0, 1, 1,  1},
    },
};

const LootTable kDesertPyramid = {
    4, 6,
    {
        {item::ITEM_ROTTEN_FLESH,  0, 3, 7, 10},
        {item::ITEM_BONE,          0, 4, 6, 10},
        {item::ITEM_GUNPOWDER,     0, 1, 8, 10},
        {item::ITEM_STRING,        0, 1, 8, 10},
        {item::ITEM_GOLDEN_APPLE,  0, 1, 1,  2},
        {item::ITEM_EMERALD,       0, 1, 3,  2},
        {item::ITEM_IRON_INGOT,    0, 1, 5,  5},
        {item::ITEM_GOLD_INGOT,    0, 1, 5,  5},
    },
};

const LootTable kJungleTemple = {
    4, 6,
    {
        {item::ITEM_DIAMOND,       0, 1, 3,  3},
        {item::ITEM_IRON_INGOT,    0, 1, 5, 10},
        {item::ITEM_GOLD_INGOT,    0, 1, 3, 15},
        {item::ITEM_EMERALD,       0, 1, 3,  2},
        {item::ITEM_ROTTEN_FLESH,  0, 3, 7, 10},
        {item::ITEM_BONE,          0, 4, 6, 10},
        {item::ITEM_STRING,        0, 1, 6, 10},
    },
};

const LootTable kNetherBridge = {
    3, 5,
    {
        {item::ITEM_GOLD_INGOT,      0, 1, 3, 15},
        {item::ITEM_IRON_INGOT,      0, 1, 5,  5},
        {item::ITEM_DIAMOND,         0, 1, 3,  5},
        {item::ITEM_GOLDEN_APPLE,    0, 1, 1,  1},
        {item::ITEM_NETHER_WART,     0, 3, 7,  5},
        {item::ITEM_SADDLE,          0, 1, 1, 10},
        {item::ITEM_FLINT_STEEL,     0, 1, 1,  5},
        {block::BlockId::OBSIDIAN,   0, 2, 4,  2},
    },
};

const LootTable kAbandonedMineshaft = {
    3, 6,
    {
        {item::ITEM_IRON_INGOT,    0, 1, 5, 10},
        {item::ITEM_GOLD_INGOT,    0, 1, 3,  5},
        {item::ITEM_REDSTONE,      0, 4, 8,  5},
        {item::ITEM_DIAMOND,       0, 1, 2,  1},
        {item::ITEM_COAL,          0, 3, 8, 10},
        {item::ITEM_BREAD,         0, 1, 1, 20},
        {item::ITEM_APPLE,         0, 1, 3, 20},
        {item::ITEM_IRON_PICKAXE,  0, 1, 1,  1},
    },
};

// splitmix64 — tiny self-contained stream for loot rolls. The generator's
// Random stays untouched (population keeps its own sequence); one call
// per drawn entry keeps this branch trivially cheap.
inline uint64_t nextRandom(uint64_t& state) {
    state += 0x9e3779b97f4a7c15ULL;
    uint64_t z = state;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

uint32_t nextBounded(uint64_t& state, uint32_t bound) {
    return bound == 0 ? 0 : (uint32_t)(nextRandom(state) % bound);
}

uint32_t poolWeight(const LootTable& table) {
    uint32_t total = 0;
    for (const auto& e : table.entries)
        total += e.weight;
    return total;
}

} // namespace

const LootTable* findTable(const std::string& table) {
    if (table == "loot_tables/chests/simple_dungeon.json")
        return &kSimpleDungeon;
    if (table == "loot_tables/chests/village_blacksmith.json")
        return &kVillageBlacksmith;
    if (table == "loot_tables/chests/village_two_room_house.json")
        return &kVillageTwoRoomHouse;
    if (table == "loot_tables/chests/desert_pyramid.json")
        return &kDesertPyramid;
    if (table == "loot_tables/chests/jungle_temple.json")
        return &kJungleTemple;
    if (table == "loot_tables/chests/nether_bridge.json")
        return &kNetherBridge;
    if (table == "loot_tables/chests/abandoned_mineshaft.json")
        return &kAbandonedMineshaft;
    return nullptr;
}

std::vector<cursestone::item::ItemStack> rollTable(const std::string& table, uint64_t seed) {
    const LootTable* t = findTable(table);
    if (!t || t->entries.empty())
        return {};

    uint64_t state = seed ^ 0xa0761d6478bd642fULL;
    const uint32_t total = poolWeight(*t);

    const uint32_t rolls = t->minRolls +
                           nextBounded(state, (uint32_t)(t->maxRolls - t->minRolls + 1));
    std::vector<cursestone::item::ItemStack> out;
    out.reserve(rolls);
    for (uint32_t i = 0; i < rolls && total > 0; ++i) {
        uint32_t pick = nextBounded(state, total);
        const LootEntry* chosen = nullptr;
        for (const auto& e : t->entries) {
            if (pick < e.weight) {
                chosen = &e;
                break;
            }
            pick -= e.weight;
        }
        if (!chosen)
            continue;
        cursestone::item::ItemStack stack;
        stack.id = chosen->id;
        stack.meta = chosen->meta;
        stack.count = (int)(chosen->minCount +
                            nextBounded(state, (uint32_t)(chosen->maxCount - chosen->minCount + 1)));
        if (stack.count > 0)
            out.push_back(stack);
    }
    return out;
}

} // namespace cursestone::loot
