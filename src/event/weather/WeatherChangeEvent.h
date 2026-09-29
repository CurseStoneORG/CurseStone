// SPDX-License-Identifier: LGPL-3.0-or-later
// WeatherChangeEvent.h — ported from cn.nukkit.event.weather.WeatherChangeEvent
//
// Fired when the rain / weather state of a level changes. Cancellable.
#pragma once
#include "WeatherEvent.h"
#include "../Cancellable.h"
#include <string>
namespace cursestone { namespace level { class Level; } }

namespace cursestone::event::weather {

class WeatherChangeEvent : public WeatherEvent, public Cancellable {
public:
    bool to;

    WeatherChangeEvent(cursestone::level::Level* l, bool t)
        : WeatherEvent(l), to(t) {}

    const std::string& eventName() const override {
        static std::string n = "WeatherChangeEvent"; return n;
    }
};

} // namespace cursestone::event::weather
