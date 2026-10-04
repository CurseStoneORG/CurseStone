// SPDX-License-Identifier: LGPL-3.0-or-later
// WeatherChangeEvent.h — ported from cn.nukkit.event.level.WeatherChangeEvent
//
// Fired when the rain / weather state of a level changes. Cancellable.
#pragma once
#include "WeatherEvent.h"
#include "../Cancellable.h"
#include <string>
namespace cursestone { namespace world { class World; } }

namespace cursestone::event::world {

class WeatherChangeEvent : public WeatherEvent, public Cancellable {
public:
    bool to;

    WeatherChangeEvent(cursestone::world::World* l, bool t)
        : WeatherEvent(l), to(t) {}

    const std::string& eventName() const override {
        static std::string n = "WeatherChangeEvent"; return n;
    }
};

} // namespace cursestone::event::world
