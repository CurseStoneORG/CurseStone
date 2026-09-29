// SPDX-License-Identifier: LGPL-3.0-or-later
// InventoryCloseEvent.h — ported from cn.nukkit.event.inventory.InventoryCloseEvent
#pragma once
#include "InventoryEvent.h"
namespace cursestone::event::inventory {

class InventoryCloseEvent : public InventoryEvent {
public:
    cursestone::Player* who;
    InventoryCloseEvent(std::shared_ptr<cursestone::inventory::Inventory> inv, cursestone::Player* p)
        : InventoryEvent(std::move(inv)), who(p) {}
    cursestone::Player* getPlayer() { return who; }
    const std::string& eventName() const override {
        static std::string n = "InventoryCloseEvent"; return n;
    }
};

} // namespace cursestone::event::inventory
