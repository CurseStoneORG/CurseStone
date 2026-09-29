// SPDX-License-Identifier: LGPL-3.0-or-later
// PlayerCreationEvent.h — ported from cn.nukkit.event.player.PlayerCreationEvent
#pragma once
#include "PlayerEvent.h"
#include <string>
namespace cursestone { class Player; }

namespace cursestone::event::player {
class PlayerCreationEvent : public PlayerEvent {
public:
    std::string username;
    PlayerCreationEvent(cursestone::Player* p, const std::string& name)
        : PlayerEvent(p), username(name) {}
    const std::string& eventName() const override {
        static std::string n = "PlayerCreationEvent"; return n;
    }
};
} // namespace cursestone::event::player
