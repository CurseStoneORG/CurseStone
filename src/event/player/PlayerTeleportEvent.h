// SPDX-License-Identifier: LGPL-3.0-or-later
// PlayerTeleportEvent.h — ported from cn.nukkit.event.player.PlayerTeleportEvent
#pragma once
#include "PlayerEvent.h"
#include "../Cancellable.h"
namespace atomikpe { class Player; }

namespace atomikpe::event::player {
class PlayerTeleportEvent : public PlayerEvent, public Cancellable {
public:
    double fromX = 0.0;
    double fromY = 0.0;
    double fromZ = 0.0;
    double toX = 0.0;
    double toY = 0.0;
    double toZ = 0.0;
    PlayerTeleportEvent(atomikpe::Player* p, double fx, double fy, double fz,
                        double tx, double ty, double tz)
        : PlayerEvent(p), fromX(fx), fromY(fy), fromZ(fz),
          toX(tx), toY(ty), toZ(tz) {}
    const std::string& eventName() const override {
        static std::string n = "PlayerTeleportEvent"; return n;
    }
};
} // namespace atomikpe::event::player
