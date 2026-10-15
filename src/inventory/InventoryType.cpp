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
// SPDX-License-Identifier: LGPL-3.0-or-later
// InventoryType.cpp — ported from cn.nukkit.inventory.InventoryType
#include "InventoryType.h"

namespace cursestone::inventory {

std::unordered_map<int, std::shared_ptr<InventoryType>>& InventoryType::defaults() {
    static std::unordered_map<int, std::shared_ptr<InventoryType>> d;
    return d;
}

bool InventoryType::initialized_ = false;

void InventoryType::init() {
    if (initialized_)
        return;
    auto& d = defaults();
    d[CHEST] = std::make_shared<InventoryType>(27, "Chest", 0);
    d[DOUBLE_CHEST] = std::make_shared<InventoryType>(27 + 27, "Double Chest", 0);
    d[PLAYER] = std::make_shared<InventoryType>(49, "Player",
                                                0); // 36 container + 4 armor (9 hotbar reference)
    d[FURNACE] = std::make_shared<InventoryType>(3, "Furnace", 2);
    d[CRAFTING] = std::make_shared<InventoryType>(5, "Crafting", 1);
    d[WORKBENCH] = std::make_shared<InventoryType>(10, "Crafting", 1);
    d[STONECUTTER] = std::make_shared<InventoryType>(10, "Crafting", 1);
    d[ENCHANT_TABLE] = std::make_shared<InventoryType>(2, "Enchant", 3);
    d[BREWING_STAND] = std::make_shared<InventoryType>(4, "Brewing", 4);
    d[ANVIL] = std::make_shared<InventoryType>(3, "Anvil", 5);
    d[DISPENSER] = std::make_shared<InventoryType>(9, "Dispenser", 6);
    d[DROPPER] = std::make_shared<InventoryType>(9, "Dropper", 7);
    d[HOPPER] = std::make_shared<InventoryType>(5, "Hopper", 8);
    initialized_ = true;
}

std::shared_ptr<InventoryType> InventoryType::get(int index) {
    if (!initialized_)
        init();
    auto it = defaults().find(index);
    if (it == defaults().end())
        return nullptr;
    return it->second;
}

} // namespace cursestone::inventory
