// SPDX-License-Identifier: LGPL-3.0-or-later
// PlayerEatFoodEvent.h — ported from cn.nukkit.event.player.PlayerEatFoodEvent
#pragma once
#include "PlayerEvent.h"
#include "../Cancellable.h"
#include "../../item/ItemStack.h"
namespace cursestone { class Player; }

namespace cursestone::event::player {
class PlayerEatFoodEvent : public PlayerEvent, public Cancellable {
public:
    cursestone::item::ItemStack food;
    PlayerEatFoodEvent(cursestone::Player* p, const cursestone::item::ItemStack& f)
        : PlayerEvent(p), food(f) {}
    const std::string& eventName() const override {
        static std::string n = "PlayerEatFoodEvent"; return n;
    }
};
} // namespace cursestone::event::player
