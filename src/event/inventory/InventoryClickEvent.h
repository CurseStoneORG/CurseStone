// SPDX-License-Identifier: LGPL-3.0-or-later
// InventoryClickEvent.h — ported from cn.nukkit.event.inventory.InventoryClickEvent
#pragma once
#include "InventoryEvent.h"
#include "../Cancellable.h"
#include "../../item/ItemStack.h"
namespace cursestone::event::inventory {

class InventoryClickEvent : public InventoryEvent, public Cancellable {
public:
    cursestone::Player* who;
    int slot;
    cursestone::item::ItemStack item;
    InventoryClickEvent(std::shared_ptr<cursestone::inventory::Inventory> inv,
                          cursestone::Player* p, int s, const cursestone::item::ItemStack& it)
        : InventoryEvent(std::move(inv)), who(p), slot(s), item(it) {}
    cursestone::Player* getWhoClicked() { return who; }
    int getSlot() { return slot; }
    cursestone::item::ItemStack getItem() { return item; }
    const std::string& eventName() const override {
        static std::string n = "InventoryClickEvent"; return n;
    }
};

} // namespace cursestone::event::inventory
