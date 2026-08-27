// SPDX-License-Identifier: LGPL-3.0-or-later
// PlayerToggleSprintEvent.h — ported from cn.nukkit.event.player.PlayerToggleSprintEvent
#pragma once
#include "PlayerEvent.h"
#include "../Cancellable.h"
namespace atomikpe { class Player; }

namespace atomikpe::event::player {
class PlayerToggleSprintEvent : public PlayerEvent, public Cancellable {
public:
    bool isSprinting = false;
    PlayerToggleSprintEvent(atomikpe::Player* p, bool sprinting)
        : PlayerEvent(p), isSprinting(sprinting) {}
    const std::string& eventName() const override {
        static std::string n = "PlayerToggleSprintEvent"; return n;
    }
};
} // namespace atomikpe::event::player
