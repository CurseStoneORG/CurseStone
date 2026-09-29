// SPDX-License-Identifier: LGPL-3.0-or-later
// PlayerFoodLevelChangeEvent.h — ported from cn.nukkit.event.player.PlayerFoodLevelChangeEvent
#pragma once
#include "PlayerEvent.h"
#include "../Cancellable.h"
namespace cursestone { class Player; }

namespace cursestone::event::player {
class PlayerFoodLevelChangeEvent : public PlayerEvent, public Cancellable {
public:
    int foodLevel = 0;
    PlayerFoodLevelChangeEvent(cursestone::Player* p, int fl)
        : PlayerEvent(p), foodLevel(fl) {}
    const std::string& eventName() const override {
        static std::string n = "PlayerFoodLevelChangeEvent"; return n;
    }
};
} // namespace cursestone::event::player
