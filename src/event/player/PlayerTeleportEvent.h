/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

 * CurseStone | Open Source Server Software
 *
 * Copyright (C) 2026 CurseStoneTeam
 *
 * Developed by CurseStoneTeam.
 * Built for an open and extensible Minecraft server ecosystem.
 *
 * Licensed under the GNU Lesser General Public License v3.0 or later.
 * You may study, modify, and redistribute this software under its license.
 *
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 * Repository: https://github.com/CurseStoneTeam/CurseStone
 *
 */
// SPDX-License-Identifier: LGPL-3.0-or-later
// PlayerTeleportEvent.h — ported from cn.nukkit.event.player.PlayerTeleportEvent
#pragma once
#include "PlayerEvent.h"
#include "../Cancellable.h"
namespace cursestone {
class Player;
}

namespace cursestone::event::player {
class PlayerTeleportEvent : public PlayerEvent, public Cancellable {
  public:
    double fromX = 0.0;
    double fromY = 0.0;
    double fromZ = 0.0;
    double toX = 0.0;
    double toY = 0.0;
    double toZ = 0.0;
    PlayerTeleportEvent(cursestone::Player* p, double fx, double fy, double fz, double tx,
                        double ty, double tz)
        : PlayerEvent(p), fromX(fx), fromY(fy), fromZ(fz), toX(tx), toY(ty), toZ(tz) {
    }
    const std::string& eventName() const override {
        static std::string n = "PlayerTeleportEvent";
        return n;
    }
};
} // namespace cursestone::event::player
