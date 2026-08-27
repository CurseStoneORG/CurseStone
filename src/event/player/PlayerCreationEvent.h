// SPDX-License-Identifier: LGPL-3.0-or-later
// PlayerCreationEvent.h — ported from cn.nukkit.event.player.PlayerCreationEvent
#pragma once
#include "PlayerEvent.h"
#include <string>
namespace atomikpe { class Player; }

namespace atomikpe::event::player {
class PlayerCreationEvent : public PlayerEvent {
public:
    std::string username;
    PlayerCreationEvent(atomikpe::Player* p, const std::string& name)
        : PlayerEvent(p), username(name) {}
    const std::string& eventName() const override {
        static std::string n = "PlayerCreationEvent"; return n;
    }
};
} // namespace atomikpe::event::player
