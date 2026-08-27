// SPDX-License-Identifier: LGPL-3.0-or-later
// LevelEvent.h — ported from cn.nukkit.event.level.LevelEvent
//
// Base class for all level-related events. Holds a reference to the
// level the event occurred in.
#pragma once
#include "../Event.h"
#include <string>
namespace atomikpe { namespace level { class Level; } }

namespace atomikpe::event::level {

class LevelEvent : public Event {
public:
    atomikpe::level::Level* level;

    explicit LevelEvent(atomikpe::level::Level* l)
        : level(l) {}

    const std::string& eventName() const override {
        static std::string n = "LevelEvent"; return n;
    }
};

} // namespace atomikpe::event::level
