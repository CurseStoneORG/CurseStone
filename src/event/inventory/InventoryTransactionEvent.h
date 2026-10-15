/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

 * CurseStone | Open Source Server Software
 *
 * Copyright (C) 2026 CurseStoneTeam
 *
 * Developed by CurseStoneTeam.
 * Built for an open and extensible Minecraft server ecosystem.
 *
 * Licensed under the GNU Lesser General Public License v3.0 or later.
 * You may study, modify, and redistribute this software under its license.
 *
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 * Repository: https://github.com/CurseStoneTeam/CurseStone
 *
 */
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
    explicit InventoryTransactionEvent(TransactionGroup* tg) : transaction(tg) {
    }
    TransactionGroup* getTransaction() {
        return transaction;
    }
    const std::string& eventName() const override {
        static std::string n = "InventoryTransactionEvent";
        return n;
    }
};

} // namespace cursestone::event::inventory
