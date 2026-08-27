// SPDX-License-Identifier: LGPL-3.0-or-later
// PlayerEatFoodEvent.h — ported from cn.nukkit.event.player.PlayerEatFoodEvent
#pragma once
#include "PlayerEvent.h"
#include "../Cancellable.h"
#include "../../item/ItemStack.h"
namespace atomikpe { class Player; }

namespace atomikpe::event::player {
class PlayerEatFoodEvent : public PlayerEvent, public Cancellable {
public:
    atomikpe::item::ItemStack food;
    PlayerEatFoodEvent(atomikpe::Player* p, const atomikpe::item::ItemStack& f)
        : PlayerEvent(p), food(f) {}
    const std::string& eventName() const override {
        static std::string n = "PlayerEatFoodEvent"; return n;
    }
};
} // namespace atomikpe::event::player
