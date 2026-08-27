// SPDX-License-Identifier: LGPL-3.0-or-later
// CraftItemEvent.h — ported from cn.nukkit.event.inventory.CraftItemEvent
#pragma once
#include "../Event.h"
#include "../Cancellable.h"
#include "../../item/ItemStack.h"
#include <vector>
namespace atomikpe { class Player; }

namespace atomikpe::event::inventory {

// Forward-declare Recipe — will be a proper class when the crafting
// system is ported.
class Recipe;

class CraftItemEvent : public Event, public Cancellable {
public:
    std::vector<atomikpe::item::ItemStack> input;
    Recipe* recipe;
    atomikpe::Player* player;
    CraftItemEvent(atomikpe::Player* p, std::vector<atomikpe::item::ItemStack> in, Recipe* r)
        : input(std::move(in)), recipe(r), player(p) {}
    std::vector<atomikpe::item::ItemStack> getInput() { return input; }
    Recipe* getRecipe() { return recipe; }
    atomikpe::Player* getPlayer() { return player; }
    const std::string& eventName() const override {
        static std::string n = "CraftItemEvent"; return n;
    }
};

} // namespace atomikpe::event::inventory
