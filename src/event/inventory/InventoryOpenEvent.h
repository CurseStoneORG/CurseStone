// SPDX-License-Identifier: LGPL-3.0-or-later
// InventoryOpenEvent.h — ported from cn.nukkit.event.inventory.InventoryOpenEvent
#pragma once
#include "InventoryEvent.h"
#include "../Cancellable.h"
namespace cursestone::event::inventory {

class InventoryOpenEvent : public InventoryEvent, public Cancellable {
public:
    cursestone::Player* who;
    InventoryOpenEvent(std::shared_ptr<cursestone::inventory::Inventory> inv, cursestone::Player* p)
        : InventoryEvent(std::move(inv)), who(p) {}
    cursestone::Player* getPlayer() { return who; }
    const std::string& eventName() const override {
        static std::string n = "InventoryOpenEvent"; return n;
    }
};

} // namespace cursestone::event::inventory
