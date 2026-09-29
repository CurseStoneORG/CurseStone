// SPDX-License-Identifier: LGPL-3.0-or-later
// CraftItemEvent.h — ported from cn.nukkit.event.inventory.CraftItemEvent
#pragma once
#include "../Event.h"
#include "../Cancellable.h"
#include "../../item/ItemStack.h"
#include <vector>
namespace cursestone { class Player; }

namespace cursestone::event::inventory {

// Forward-declare Recipe — will be a proper class when the crafting
// system is ported.
class Recipe;

class CraftItemEvent : public Event, public Cancellable {
public:
    std::vector<cursestone::item::ItemStack> input;
    Recipe* recipe;
    cursestone::Player* player;
    CraftItemEvent(cursestone::Player* p, std::vector<cursestone::item::ItemStack> in, Recipe* r)
        : input(std::move(in)), recipe(r), player(p) {}
    std::vector<cursestone::item::ItemStack> getInput() { return input; }
    Recipe* getRecipe() { return recipe; }
    cursestone::Player* getPlayer() { return player; }
    const std::string& eventName() const override {
        static std::string n = "CraftItemEvent"; return n;
    }
};

} // namespace cursestone::event::inventory
