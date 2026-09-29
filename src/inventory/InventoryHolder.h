// SPDX-License-Identifier: LGPL-3.0-or-later
// InventoryHolder.h — ported from cn.nukkit.inventory.InventoryHolder
//
// Anything that "holds" an inventory (Player, Chest tile, Furnace tile)
// implements this interface so the inventory can ask its holder for
// context (e.g. who to send packets to, where to spawn the container).
#pragma once
#include <memory>
namespace cursestone::inventory {

class Inventory;

class InventoryHolder {
public:
    virtual ~InventoryHolder() = default;
    // Returns the inventory this holder owns. May return nullptr if the
    // holder has been closed/destroyed.
    virtual std::shared_ptr<Inventory> getInventory() = 0;
};

} // namespace cursestone::inventory
