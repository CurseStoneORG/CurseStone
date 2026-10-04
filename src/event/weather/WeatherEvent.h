// SPDX-License-Identifier: LGPL-3.0-or-later
// WeatherEvent.h — ported from cn.nukkit.event.weather.WeatherEvent
//
// Base class for all weather events (lightning, thunder / rain state
// changes). Holds a reference to the affected world.
#pragma once
#include "../Event.h"
#include <string>
namespace cursestone { namespace world { class World; } }

namespace cursestone::event::weather {

class WeatherEvent : public Event {
public:
    cursestone::world::World* world;

    explicit WeatherEvent(cursestone::world::World* l)
        : world(l) {}

    const std::string& eventName() const override {
        static std::string n = "WeatherEvent"; return n;
    }
};

} // namespace cursestone::event::weather
