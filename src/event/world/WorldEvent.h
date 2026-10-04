// SPDX-License-Identifier: LGPL-3.0-or-later
// LevelEvent.h — ported from cn.nukkit.event.level.LevelEvent
//
// Base class for all level-related events. Holds a reference to the
// level the event occurred in.
#pragma once
#include "../Event.h"
#include <string>
namespace cursestone { namespace world { class World; } }

namespace cursestone::event::world {

class LevelEvent : public Event {
public:
    cursestone::world::World* level;

    explicit LevelEvent(cursestone::world::World* l)
        : level(l) {}

    const std::string& eventName() const override {
        static std::string n = "LevelEvent"; return n;
    }
};

} // namespace cursestone::event::world
