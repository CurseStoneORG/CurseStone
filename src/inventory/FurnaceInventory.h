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
// FurnaceInventory — 3-slot container inventory (mirrors Nukkit
// FurnaceInventory).
//
// Es el backing store real de TileFurnace (misma arquitectura que
// ChestInventory for TileChest): one place where the 3 slots live
// slots (0=INPUT, 1=FUEL, 2=RESULT) para que inventario, NBT y UI no
// puedan divergir.
#pragma once
#include "BaseInventory.h"
#include "InventoryType.h"
namespace cursestone::inventory {

class FurnaceInventory : public BaseInventory {
  public:
    FurnaceInventory() : BaseInventory(nullptr, InventoryType::get(InventoryType::FURNACE)) {
    }

    // Slot indices (Nukkit convention).
    static constexpr int INPUT = 0;
    static constexpr int FUEL = 1;
    static constexpr int RESULT = 2;
};

} // namespace cursestone::inventory
