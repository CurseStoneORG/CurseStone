// SPDX-License-Identifier: LGPL-3.0-or-later
// InventoryPickupArrowEvent.h — ported from cn.nukkit.event.inventory.InventoryPickupArrowEvent
#pragma once
#include "InventoryEvent.h"
#include "../Cancellable.h"
namespace atomikpe::entity { class EntityArrow; }

namespace atomikpe::event::inventory {

class InventoryPickupArrowEvent : public InventoryEvent, public Cancellable {
public:
    atomikpe::entity::EntityArrow* arrow;
    InventoryPickupArrowEvent(std::shared_ptr<atomikpe::inventory::Inventory> inv,
                                atomikpe::entity::EntityArrow* a)
        : InventoryEvent(std::move(inv)), arrow(a) {}
    atomikpe::entity::EntityArrow* getArrow() { return arrow; }
    const std::string& eventName() const override {
        static std::string n = "InventoryPickupArrowEvent"; return n;
    }
};

} // namespace atomikpe::event::inventory
