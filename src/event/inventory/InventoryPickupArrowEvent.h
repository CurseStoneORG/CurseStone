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
// InventoryPickupArrowEvent.h — ported from cn.nukkit.event.inventory.InventoryPickupArrowEvent
#pragma once
#include "InventoryEvent.h"
#include "../Cancellable.h"
namespace cursestone::entity {
class EntityArrow;
}

namespace cursestone::event::inventory {

class InventoryPickupArrowEvent : public InventoryEvent, public Cancellable {
  public:
    cursestone::entity::EntityArrow* arrow;
    InventoryPickupArrowEvent(std::shared_ptr<cursestone::inventory::Inventory> inv,
                              cursestone::entity::EntityArrow* a)
        : InventoryEvent(std::move(inv)), arrow(a) {
    }
    cursestone::entity::EntityArrow* getArrow() {
        return arrow;
    }
    const std::string& eventName() const override {
        static std::string n = "InventoryPickupArrowEvent";
        return n;
    }
};

} // namespace cursestone::event::inventory
