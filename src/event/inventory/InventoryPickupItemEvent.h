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
// InventoryPickupItemEvent.h — ported from cn.nukkit.event.inventory.InventoryPickupItemEvent
#pragma once
#include "InventoryEvent.h"
#include "../Cancellable.h"
#include "../../world/World.h"
namespace cursestone::event::inventory {

class InventoryPickupItemEvent : public InventoryEvent, public Cancellable {
public:
    cursestone::world::DroppedItem* item;
    InventoryPickupItemEvent(std::shared_ptr<cursestone::inventory::Inventory> inv,
                               cursestone::world::DroppedItem* di)
        : InventoryEvent(std::move(inv)), item(di) {}
    cursestone::world::DroppedItem* getItem() { return item; }
    const std::string& eventName() const override {
        static std::string n = "InventoryPickupItemEvent"; return n;
    }
};

} // namespace cursestone::event::inventory
