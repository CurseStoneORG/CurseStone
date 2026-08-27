// SPDX-License-Identifier: LGPL-3.0-or-later
// InventoryClickEvent.h — ported from cn.nukkit.event.inventory.InventoryClickEvent
#pragma once
#include "InventoryEvent.h"
#include "../Cancellable.h"
#include "../../item/ItemStack.h"
namespace atomikpe::event::inventory {

class InventoryClickEvent : public InventoryEvent, public Cancellable {
public:
    atomikpe::Player* who;
    int slot;
    atomikpe::item::ItemStack item;
    InventoryClickEvent(std::shared_ptr<atomikpe::inventory::Inventory> inv,
                          atomikpe::Player* p, int s, const atomikpe::item::ItemStack& it)
        : InventoryEvent(std::move(inv)), who(p), slot(s), item(it) {}
    atomikpe::Player* getWhoClicked() { return who; }
    int getSlot() { return slot; }
    atomikpe::item::ItemStack getItem() { return item; }
    const std::string& eventName() const override {
        static std::string n = "InventoryClickEvent"; return n;
    }
};

} // namespace atomikpe::event::inventory
