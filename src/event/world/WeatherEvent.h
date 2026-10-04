// SPDX-License-Identifier: LGPL-3.0-or-later
// WeatherEvent.h — ported from cn.nukkit.event.level.WeatherEvent
//
// Base class for all weather-related level events (rain / thunder
// state changes). Holds a reference to the affected level.
#pragma once
#include "../Event.h"
#include <string>
namespace cursestone { namespace world { class World; } }

namespace cursestone::event::world {

class WeatherEvent : public Event {
public:
    cursestone::world::World* level;

    explicit WeatherEvent(cursestone::world::World* l)
        : level(l) {}

    const std::string& eventName() const override {
        static std::string n = "WeatherEvent"; return n;
    }
};

} // namespace cursestone::event::world
