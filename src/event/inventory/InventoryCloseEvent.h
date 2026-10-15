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
// InventoryCloseEvent.h — ported from cn.nukkit.event.inventory.InventoryCloseEvent
#pragma once
#include "InventoryEvent.h"
namespace cursestone::event::inventory {

class InventoryCloseEvent : public InventoryEvent {
  public:
    cursestone::Player* who;
    InventoryCloseEvent(std::shared_ptr<cursestone::inventory::Inventory> inv,
                        cursestone::Player* p)
        : InventoryEvent(std::move(inv)), who(p) {
    }
    cursestone::Player* getPlayer() {
        return who;
    }
    const std::string& eventName() const override {
        static std::string n = "InventoryCloseEvent";
        return n;
    }
};

} // namespace cursestone::event::inventory
