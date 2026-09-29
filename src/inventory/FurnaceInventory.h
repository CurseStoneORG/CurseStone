// SPDX-License-Identifier: LGPL-3.0-or-later
// FurnaceInventory — 3-slot container inventory (mirrors Nukkit FurnaceInventory).
#pragma once
#include "BaseInventory.h"
#include "InventoryType.h"
namespace cursestone::inventory {
class FurnaceInventory : public BaseInventory {
public:
    FurnaceInventory()
        : BaseInventory(nullptr, InventoryType::get(InventoryType::FURNACE)) {}
    int INPUT  = 0;
    int FUEL   = 1;
    int RESULT = 2;
};
} // namespace
