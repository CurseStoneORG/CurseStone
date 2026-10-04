// SPDX-License-Identifier: LGPL-3.0-or-later
// WorldEvent.h — ported from cn.nukkit.event.level.LevelEvent
//
// Base class for all world-related events. Holds a reference to the
// world the event occurred in.
#pragma once
#include "../Event.h"
#include <string>
namespace cursestone { namespace world { class World; } }

namespace cursestone::event::world {

class WorldEvent : public Event {
public:
    cursestone::world::World* world;

    explicit WorldEvent(cursestone::world::World* l)
        : world(l) {}

    const std::string& eventName() const override {
        static std::string n = "WorldEvent"; return n;
    }
};

} // namespace cursestone::event::world
