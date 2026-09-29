// SPDX-License-Identifier: LGPL-3.0-or-later
// PlayerGameModeChangeEvent.h — ported from cn.nukkit.event.player.PlayerGameModeChangeEvent
#pragma once
#include "PlayerEvent.h"
#include "../Cancellable.h"
namespace cursestone { class Player; }

namespace cursestone::event::player {
class PlayerGameModeChangeEvent : public PlayerEvent, public Cancellable {
public:
    int newGamemode = 0;
    PlayerGameModeChangeEvent(cursestone::Player* p, int gm)
        : PlayerEvent(p), newGamemode(gm) {}
    const std::string& eventName() const override {
        static std::string n = "PlayerGameModeChangeEvent"; return n;
    }
};
} // namespace cursestone::event::player
