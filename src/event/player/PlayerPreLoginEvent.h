// SPDX-License-Identifier: LGPL-3.0-or-later
// PlayerPreLoginEvent.h — ported from cn.nukkit.event.player.PlayerPreLoginEvent
#pragma once
#include "PlayerEvent.h"
#include "../Cancellable.h"
#include <string>
namespace cursestone { class Player; }

namespace cursestone::event::player {
class PlayerPreLoginEvent : public PlayerEvent, public Cancellable {
public:
    std::string message;
    PlayerPreLoginEvent(cursestone::Player* p, const std::string& msg = "")
        : PlayerEvent(p), message(msg) {}
    const std::string& eventName() const override {
        static std::string n = "PlayerPreLoginEvent"; return n;
    }
};
} // namespace cursestone::event::player
