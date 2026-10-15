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
// InventoryEvent.h — ported from cn.nukkit.event.inventory.InventoryEvent
//
// Base class for all inventory-related events. Holds a reference to the
// inventory that triggered the event. Subclasses: InventoryOpenEvent,
// InventoryCloseEvent, InventoryClickEvent, InventoryPickupItemEvent,
// InventoryPickupArrowEvent.
#pragma once
#include "../Event.h"
#include "../../inventory/Inventory.h"
#include <memory>
namespace cursestone {
class Player;
}
namespace cursestone::event::inventory {

class InventoryEvent : public Event {
  public:
    std::shared_ptr<cursestone::inventory::Inventory> inv;

    explicit InventoryEvent(std::shared_ptr<cursestone::inventory::Inventory> inventory)
        : inv(std::move(inventory)) {
    }

    std::shared_ptr<cursestone::inventory::Inventory> getInventory() {
        return inv;
    }

    const std::string& eventName() const override {
        static std::string n = "InventoryEvent";
        return n;
    }
};

} // namespace cursestone::event::inventory
