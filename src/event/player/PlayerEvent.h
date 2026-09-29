// SPDX-License-Identifier: LGPL-3.0-or-later
// PlayerEvent.h — ported from cn.nukkit.event.player.PlayerEvent
//
// Base class for all player-related events. Holds a reference to the
// player that triggered the event.
#pragma once
#include "../Event.h"
namespace cursestone { class Player; }

namespace cursestone::event::player {

class PlayerEvent : public Event {
public:
    cursestone::Player* player = nullptr;
    explicit PlayerEvent(cursestone::Player* p = nullptr) : player(p) {}
    cursestone::Player* getPlayer() { return player; }
    const std::string& eventName() const override {
        static std::string n = "PlayerEvent"; return n;
    }
};

} // namespace cursestone::event::player
