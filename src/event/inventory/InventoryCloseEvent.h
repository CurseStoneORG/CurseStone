// SPDX-License-Identifier: LGPL-3.0-or-later
// InventoryCloseEvent.h — ported from cn.nukkit.event.inventory.InventoryCloseEvent
#pragma once
#include "InventoryEvent.h"
namespace atomikpe::event::inventory {

class InventoryCloseEvent : public InventoryEvent {
public:
    atomikpe::Player* who;
    InventoryCloseEvent(std::shared_ptr<atomikpe::inventory::Inventory> inv, atomikpe::Player* p)
        : InventoryEvent(std::move(inv)), who(p) {}
    atomikpe::Player* getPlayer() { return who; }
    const std::string& eventName() const override {
        static std::string n = "InventoryCloseEvent"; return n;
    }
};

} // namespace atomikpe::event::inventory
