// SPDX-License-Identifier: LGPL-3.0-or-later
// PlayerBedEnterEvent.h — ported from cn.nukkit.event.player.PlayerBedEnterEvent
#pragma once
#include "PlayerEvent.h"
#include "../Cancellable.h"
namespace atomikpe { class Player; }

namespace atomikpe::event::player {
class PlayerBedEnterEvent : public PlayerEvent, public Cancellable {
public:
    int x = 0;
    int y = 0;
    int z = 0;
    PlayerBedEnterEvent(atomikpe::Player* p, int bx, int by, int bz)
        : PlayerEvent(p), x(bx), y(by), z(bz) {}
    const std::string& eventName() const override {
        static std::string n = "PlayerBedEnterEvent"; return n;
    }
};
} // namespace atomikpe::event::player
