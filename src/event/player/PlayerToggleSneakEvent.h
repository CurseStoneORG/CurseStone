// SPDX-License-Identifier: LGPL-3.0-or-later
// PlayerToggleSneakEvent.h — ported from cn.nukkit.event.player.PlayerToggleSneakEvent
#pragma once
#include "PlayerEvent.h"
#include "../Cancellable.h"
namespace atomikpe { class Player; }

namespace atomikpe::event::player {
class PlayerToggleSneakEvent : public PlayerEvent, public Cancellable {
public:
    bool isSneaking = false;
    PlayerToggleSneakEvent(atomikpe::Player* p, bool sneaking)
        : PlayerEvent(p), isSneaking(sneaking) {}
    const std::string& eventName() const override {
        static std::string n = "PlayerToggleSneakEvent"; return n;
    }
};
} // namespace atomikpe::event::player
