// SPDX-License-Identifier: LGPL-3.0-or-later
// InventoryPickupItemEvent.h — ported from cn.nukkit.event.inventory.InventoryPickupItemEvent
#pragma once
#include "InventoryEvent.h"
#include "../Cancellable.h"
#include "../../world/World.h"
namespace cursestone::event::inventory {

class InventoryPickupItemEvent : public InventoryEvent, public Cancellable {
public:
    cursestone::world::DroppedItem* item;
    InventoryPickupItemEvent(std::shared_ptr<cursestone::inventory::Inventory> inv,
                               cursestone::world::DroppedItem* di)
        : InventoryEvent(std::move(inv)), item(di) {}
    cursestone::world::DroppedItem* getItem() { return item; }
    const std::string& eventName() const override {
        static std::string n = "InventoryPickupItemEvent"; return n;
    }
};

} // namespace cursestone::event::inventory
