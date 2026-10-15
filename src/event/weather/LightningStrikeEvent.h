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
// LightningStrikeEvent.h — ported from cn.nukkit.event.weather.LightningStrikeEvent
//
// Fired when a lightning bolt is about to strike a world. Cancellable.
#pragma once
#include "WeatherEvent.h"
#include "../Cancellable.h"
#include <string>
namespace cursestone {
namespace world {
class World;
}
}

namespace cursestone::event::weather {

class LightningStrikeEvent : public WeatherEvent, public Cancellable {
  public:
    void* bolt;

    LightningStrikeEvent(cursestone::world::World* l, void* b) : WeatherEvent(l), bolt(b) {
    }

    const std::string& eventName() const override {
        static std::string n = "LightningStrikeEvent";
        return n;
    }
};

} // namespace cursestone::event::weather
