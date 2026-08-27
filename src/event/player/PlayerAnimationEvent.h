// SPDX-License-Identifier: LGPL-3.0-or-later
// PlayerAnimationEvent.h — ported from cn.nukkit.event.player.PlayerAnimationEvent
#pragma once
#include "PlayerEvent.h"
#include "../Cancellable.h"
namespace atomikpe { class Player; }

namespace atomikpe::event::player {
class PlayerAnimationEvent : public PlayerEvent, public Cancellable {
public:
    int animationType = 0;
    PlayerAnimationEvent(atomikpe::Player* p, int type)
        : PlayerEvent(p), animationType(type) {}
    const std::string& eventName() const override {
        static std::string n = "PlayerAnimationEvent"; return n;
    }
};
} // namespace atomikpe::event::player
