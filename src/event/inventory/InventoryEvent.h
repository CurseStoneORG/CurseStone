// SPDX-License-Identifier: LGPL-3.0-or-later
// InventoryEvent.h — ported from cn.nukkit.event.inventory.InventoryEvent
//
// Base class for all inventory-related events. Holds a reference to the
// inventory that triggered the event. Subclasses: InventoryOpenEvent,
// InventoryCloseEvent, InventoryClickEvent, InventoryPickupItemEvent,
// InventoryPickupArrowEvent.
#pragma once
#include "../Event.h"
#include "../../inventory/Inventory.h"
#include <memory>
namespace atomikpe { class Player; }
namespace atomikpe::event::inventory {

class InventoryEvent : public Event {
public:
    std::shared_ptr<atomikpe::inventory::Inventory> inv;

    explicit InventoryEvent(std::shared_ptr<atomikpe::inventory::Inventory> inventory)
        : inv(std::move(inventory)) {}

    std::shared_ptr<atomikpe::inventory::Inventory> getInventory() { return inv; }

    const std::string& eventName() const override {
        static std::string n = "InventoryEvent"; return n;
    }
};

} // namespace atomikpe::event::inventory
