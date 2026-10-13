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
