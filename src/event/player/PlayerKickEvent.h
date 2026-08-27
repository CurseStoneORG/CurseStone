// SPDX-License-Identifier: LGPL-3.0-or-later
// PlayerKickEvent.h — ported from cn.nukkit.event.player.PlayerKickEvent
#pragma once
#include "PlayerEvent.h"
#include "../Cancellable.h"
#include <string>
namespace atomikpe { class Player; }

namespace atomikpe::event::player {
class PlayerKickEvent : public PlayerEvent, public Cancellable {
public:
    std::string reason;
    PlayerKickEvent(atomikpe::Player* p, const std::string& r)
        : PlayerEvent(p), reason(r) {}
    const std::string& eventName() const override {
        static std::string n = "PlayerKickEvent"; return n;
    }
};
} // namespace atomikpe::event::player
