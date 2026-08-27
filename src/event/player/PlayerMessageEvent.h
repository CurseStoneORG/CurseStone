// SPDX-License-Identifier: LGPL-3.0-or-later
// PlayerMessageEvent.h — ported from cn.nukkit.event.player.PlayerMessageEvent
//
// Base class for player chat / command events. Not cancellable by
// itself — subclasses opt into Cancellable as needed.
#pragma once
#include "PlayerEvent.h"
#include <string>
namespace atomikpe { class Player; }

namespace atomikpe::event::player {
class PlayerMessageEvent : public PlayerEvent {
public:
    std::string message;
    PlayerMessageEvent(atomikpe::Player* p, const std::string& msg)
        : PlayerEvent(p), message(msg) {}
    const std::string& eventName() const override {
        static std::string n = "PlayerMessageEvent"; return n;
    }
};
} // namespace atomikpe::event::player
