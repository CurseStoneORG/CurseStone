// SPDX-License-Identifier: LGPL-3.0-or-later
// LevelUnloadEvent.h — ported from cn.nukkit.event.level.LevelUnloadEvent
//
// Fired before a level is unloaded from the server. Cancellable.
#pragma once
#include "LevelEvent.h"
#include "../Cancellable.h"
#include <string>
namespace atomikpe { namespace level { class Level; } }

namespace atomikpe::event::level {

class LevelUnloadEvent : public LevelEvent, public Cancellable {
public:
    explicit LevelUnloadEvent(atomikpe::level::Level* l)
        : LevelEvent(l) {}

    const std::string& eventName() const override {
        static std::string n = "LevelUnloadEvent"; return n;
    }
};

} // namespace atomikpe::event::level
