// SPDX-License-Identifier: LGPL-3.0-or-later
// InventoryOpenEvent.h — ported from cn.nukkit.event.inventory.InventoryOpenEvent
#pragma once
#include "InventoryEvent.h"
#include "../Cancellable.h"
namespace atomikpe::event::inventory {

class InventoryOpenEvent : public InventoryEvent, public Cancellable {
public:
    atomikpe::Player* who;
    InventoryOpenEvent(std::shared_ptr<atomikpe::inventory::Inventory> inv, atomikpe::Player* p)
        : InventoryEvent(std::move(inv)), who(p) {}
    atomikpe::Player* getPlayer() { return who; }
    const std::string& eventName() const override {
        static std::string n = "InventoryOpenEvent"; return n;
    }
};

} // namespace atomikpe::event::inventory
