// SPDX-License-Identifier: LGPL-3.0-or-later
// InventoryPickupArrowEvent.h — ported from cn.nukkit.event.inventory.InventoryPickupArrowEvent
#pragma once
#include "InventoryEvent.h"
#include "../Cancellable.h"
namespace cursestone::entity { class EntityArrow; }

namespace cursestone::event::inventory {

class InventoryPickupArrowEvent : public InventoryEvent, public Cancellable {
public:
    cursestone::entity::EntityArrow* arrow;
    InventoryPickupArrowEvent(std::shared_ptr<cursestone::inventory::Inventory> inv,
                                cursestone::entity::EntityArrow* a)
        : InventoryEvent(std::move(inv)), arrow(a) {}
    cursestone::entity::EntityArrow* getArrow() { return arrow; }
    const std::string& eventName() const override {
        static std::string n = "InventoryPickupArrowEvent"; return n;
    }
};

} // namespace cursestone::event::inventory
