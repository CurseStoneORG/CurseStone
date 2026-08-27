// SPDX-License-Identifier: LGPL-3.0-or-later
// PlayerInvalidMoveEvent.h — ported from cn.nukkit.event.player.PlayerInvalidMoveEvent
#pragma once
#include "PlayerEvent.h"
#include "../Cancellable.h"
#include <cmath>
namespace atomikpe { class Player; }

namespace atomikpe::event::player {
class PlayerInvalidMoveEvent : public PlayerEvent, public Cancellable {
public:
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    PlayerInvalidMoveEvent(atomikpe::Player* p, double px, double py, double pz)
        : PlayerEvent(p), x(px), y(py), z(pz) {}
    const std::string& eventName() const override {
        static std::string n = "PlayerInvalidMoveEvent"; return n;
    }
};
} // namespace atomikpe::event::player
