// SPDX-License-Identifier: LGPL-3.0-or-later
// InventoryTransactionEvent.h — ported from cn.nukkit.event.inventory.InventoryTransactionEvent
#pragma once
#include "../Event.h"
#include "../Cancellable.h"
namespace cursestone::event::inventory {

// Forward-declare — TransactionGroup is a future class that tracks
// multi-inventory slot changes (e.g. dragging items between containers).
class TransactionGroup;

class InventoryTransactionEvent : public Event, public Cancellable {
public:
    TransactionGroup* transaction;
    explicit InventoryTransactionEvent(TransactionGroup* tg) : transaction(tg) {}
    TransactionGroup* getTransaction() { return transaction; }
    const std::string& eventName() const override {
        static std::string n = "InventoryTransactionEvent"; return n;
    }
};

} // namespace cursestone::event::inventory
