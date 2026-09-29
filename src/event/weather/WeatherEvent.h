// SPDX-License-Identifier: LGPL-3.0-or-later
// WeatherEvent.h — ported from cn.nukkit.event.weather.WeatherEvent
//
// Base class for all weather events (lightning, thunder / rain state
// changes). Holds a reference to the affected level.
#pragma once
#include "../Event.h"
#include <string>
namespace cursestone { namespace level { class Level; } }

namespace cursestone::event::weather {

class WeatherEvent : public Event {
public:
    cursestone::level::Level* level;

    explicit WeatherEvent(cursestone::level::Level* l)
        : level(l) {}

    const std::string& eventName() const override {
        static std::string n = "WeatherEvent"; return n;
    }
};

} // namespace cursestone::event::weather
