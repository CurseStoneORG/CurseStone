// SPDX-License-Identifier: LGPL-3.0-or-later
// ThunderChangeEvent.h — ported from cn.nukkit.event.level.ThunderChangeEvent
//
// Fired when the thunder state of a world changes. Cancellable.
#pragma once
#include "WeatherEvent.h"
#include "../Cancellable.h"
#include <string>
namespace cursestone { namespace world { class World; } }

namespace cursestone::event::world {

class ThunderChangeEvent : public WeatherEvent, public Cancellable {
public:
    bool to;

    ThunderChangeEvent(cursestone::world::World* l, bool t)
        : WeatherEvent(l), to(t) {}

    const std::string& eventName() const override {
        static std::string n = "ThunderChangeEvent"; return n;
    }
};

} // namespace cursestone::event::world
