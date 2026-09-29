// SPDX-License-Identifier: LGPL-3.0-or-later
// PlayerToggleSneakEvent.h — ported from cn.nukkit.event.player.PlayerToggleSneakEvent
#pragma once
#include "PlayerEvent.h"
#include "../Cancellable.h"
namespace cursestone { class Player; }

namespace cursestone::event::player {
class PlayerToggleSneakEvent : public PlayerEvent, public Cancellable {
public:
    bool isSneaking = false;
    PlayerToggleSneakEvent(cursestone::Player* p, bool sneaking)
        : PlayerEvent(p), isSneaking(sneaking) {}
    const std::string& eventName() const override {
        static std::string n = "PlayerToggleSneakEvent"; return n;
    }
};
} // namespace cursestone::event::player
