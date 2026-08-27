// SPDX-License-Identifier: LGPL-3.0-or-later
// InventoryPickupItemEvent.h — ported from cn.nukkit.event.inventory.InventoryPickupItemEvent
#pragma once
#include "InventoryEvent.h"
#include "../Cancellable.h"
#include "../../level/Level.h"
namespace atomikpe::event::inventory {

class InventoryPickupItemEvent : public InventoryEvent, public Cancellable {
public:
    atomikpe::level::DroppedItem* item;
    InventoryPickupItemEvent(std::shared_ptr<atomikpe::inventory::Inventory> inv,
                               atomikpe::level::DroppedItem* di)
        : InventoryEvent(std::move(inv)), item(di) {}
    atomikpe::level::DroppedItem* getItem() { return item; }
    const std::string& eventName() const override {
        static std::string n = "InventoryPickupItemEvent"; return n;
    }
};

} // namespace atomikpe::event::inventory
