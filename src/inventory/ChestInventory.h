// SPDX-License-Identifier: LGPL-3.0-or-later
// ChestInventory — 27-slot container inventory (mirrors Nukkit ChestInventory).
#pragma once
#include "BaseInventory.h"
#include "InventoryType.h"
namespace cursestone::inventory {
class ChestInventory : public BaseInventory {
public:
    ChestInventory()
        : BaseInventory(nullptr, InventoryType::get(InventoryType::CHEST)) {}
};
} // namespace
