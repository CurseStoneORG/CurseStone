// SPDX-License-Identifier: LGPL-3.0-or-later
// ThunderChangeEvent.h — ported from cn.nukkit.event.level.ThunderChangeEvent
//
// Fired when the thunder state of a level changes. Cancellable.
#pragma once
#include "WeatherEvent.h"
#include "../Cancellable.h"
#include <string>
namespace atomikpe { namespace level { class Level; } }

namespace atomikpe::event::level {

class ThunderChangeEvent : public WeatherEvent, public Cancellable {
public:
    bool to;

    ThunderChangeEvent(atomikpe::level::Level* l, bool t)
        : WeatherEvent(l), to(t) {}

    const std::string& eventName() const override {
        static std::string n = "ThunderChangeEvent"; return n;
    }
};

} // namespace atomikpe::event::level
