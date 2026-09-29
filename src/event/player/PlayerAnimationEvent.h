// SPDX-License-Identifier: LGPL-3.0-or-later
// PlayerAnimationEvent.h — ported from cn.nukkit.event.player.PlayerAnimationEvent
#pragma once
#include "PlayerEvent.h"
#include "../Cancellable.h"
namespace cursestone { class Player; }

namespace cursestone::event::player {
class PlayerAnimationEvent : public PlayerEvent, public Cancellable {
public:
    int animationType = 0;
    PlayerAnimationEvent(cursestone::Player* p, int type)
        : PlayerEvent(p), animationType(type) {}
    const std::string& eventName() const override {
        static std::string n = "PlayerAnimationEvent"; return n;
    }
};
} // namespace cursestone::event::player
