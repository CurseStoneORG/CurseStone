// SPDX-License-Identifier: LGPL-3.0-or-later
// LightningStrikeEvent.h — ported from cn.nukkit.event.weather.LightningStrikeEvent
//
// Fired when a lightning bolt is about to strike a level. Cancellable.
#pragma once
#include "WeatherEvent.h"
#include "../Cancellable.h"
#include <string>
namespace cursestone { namespace level { class Level; } }

namespace cursestone::event::weather {

class LightningStrikeEvent : public WeatherEvent, public Cancellable {
public:
    void* bolt;

    LightningStrikeEvent(cursestone::level::Level* l, void* b)
        : WeatherEvent(l), bolt(b) {}

    const std::string& eventName() const override {
        static std::string n = "LightningStrikeEvent"; return n;
    }
};

} // namespace cursestone::event::weather
