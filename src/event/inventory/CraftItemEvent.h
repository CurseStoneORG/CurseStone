// SPDX-License-Identifier: LGPL-3.0-or-later
// CraftItemEvent.h — ported from cn.nukkit.event.inventory.CraftItemEvent
//
// Se dispara cuando un jugador completa un crafteo (CraftingEventPacket
// validado, o fallback del close del inventario). Un listener puede
// cancelarlo para impedir el craft (p. ej. región protegida); el server
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

    CraftItemEvent(cursestone::Player* p,
                   std::shared_ptr<cursestone::inventory::Inventory> inv,
                   const cursestone::inventory::Recipe& r)
        : InventoryEvent(std::move(inv)), who(p), recipe(&r) {}

    cursestone::Player* getPlayer() { return who; }
    const cursestone::inventory::Recipe* getRecipe() const { return recipe; }

    const std::string& eventName() const override {
        static std::string n = "CraftItemEvent"; return n;
    }
};

} // namespace cursestone::event::inventory
