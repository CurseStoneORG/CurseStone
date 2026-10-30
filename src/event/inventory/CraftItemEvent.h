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
// CraftItemEvent.h — ported from cn.nukkit.event.inventory.CraftItemEvent
//
// Se dispara cuando un jugador completa un crafteo (CraftingEventPacket
// validado, o fallback del close del inventario). Un listener puede
// cancel it to block the craft (e.g. protected region); the server
// entonces NO consume ingredientes NI da resultado, y re-sincroniza el
// inventario del jugador.
#pragma once
#include "InventoryEvent.h"
#include "../Cancellable.h"
#include "../../inventory/Recipe.h"
namespace cursestone::event::inventory {

class CraftItemEvent : public InventoryEvent, public Cancellable {
  public:
    cursestone::Player* who;
    const cursestone::inventory::Recipe* recipe;

    CraftItemEvent(cursestone::Player* p, std::shared_ptr<cursestone::inventory::Inventory> inv,
                   const cursestone::inventory::Recipe& r)
        : InventoryEvent(std::move(inv)), who(p), recipe(&r) {
    }

    cursestone::Player* getPlayer() {
        return who;
    }
    const cursestone::inventory::Recipe* getRecipe() const {
        return recipe;
    }

    const std::string& eventName() const override {
        static std::string n = "CraftItemEvent";
        return n;
    }
};

} // namespace cursestone::event::inventory
