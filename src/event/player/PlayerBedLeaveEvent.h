// SPDX-License-Identifier: LGPL-3.0-or-later
// PlayerBedLeaveEvent.h — ported from cn.nukkit.event.player.PlayerBedLeaveEvent
#pragma once
#include "PlayerEvent.h"
namespace atomikpe { class Player; }

namespace atomikpe::event::player {
class PlayerBedLeaveEvent : public PlayerEvent {
public:
    int x = 0;
    int y = 0;
    int z = 0;
    PlayerBedLeaveEvent(atomikpe::Player* p, int bx, int by, int bz)
        : PlayerEvent(p), x(bx), y(by), z(bz) {}
    const std::string& eventName() const override {
        static std::string n = "PlayerBedLeaveEvent"; return n;
    }
};
} // namespace atomikpe::event::player
