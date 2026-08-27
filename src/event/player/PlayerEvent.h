// SPDX-License-Identifier: LGPL-3.0-or-later
// PlayerEvent.h — ported from cn.nukkit.event.player.PlayerEvent
//
// Base class for all player-related events. Holds a reference to the
// player that triggered the event.
#pragma once
#include "../Event.h"
namespace atomikpe { class Player; }

namespace atomikpe::event::player {

class PlayerEvent : public Event {
public:
    atomikpe::Player* player = nullptr;
    explicit PlayerEvent(atomikpe::Player* p = nullptr) : player(p) {}
    atomikpe::Player* getPlayer() { return player; }
    const std::string& eventName() const override {
        static std::string n = "PlayerEvent"; return n;
    }
};

} // namespace atomikpe::event::player
