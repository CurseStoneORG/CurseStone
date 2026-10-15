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
// WeatherEvent.h — ported from cn.nukkit.event.weather.WeatherEvent
//
// Base class for all weather events (lightning, thunder / rain state
// changes). Holds a reference to the affected world.
#pragma once
#include "../Event.h"
#include <string>
namespace cursestone {
namespace world {
class World;
}
}

namespace cursestone::event::weather {

class WeatherEvent : public Event {
  public:
    cursestone::world::World* world;

    explicit WeatherEvent(cursestone::world::World* l) : world(l) {
    }

    const std::string& eventName() const override {
        static std::string n = "WeatherEvent";
        return n;
    }
};

} // namespace cursestone::event::weather
