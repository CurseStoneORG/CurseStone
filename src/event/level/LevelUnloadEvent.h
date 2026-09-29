// SPDX-License-Identifier: LGPL-3.0-or-later
// LevelUnloadEvent.h — ported from cn.nukkit.event.level.LevelUnloadEvent
//
// Fired before a level is unloaded from the server. Cancellable.
#pragma once
#include "LevelEvent.h"
#include "../Cancellable.h"
#include <string>
namespace cursestone { namespace level { class Level; } }

namespace cursestone::event::level {

class LevelUnloadEvent : public LevelEvent, public Cancellable {
public:
    explicit LevelUnloadEvent(cursestone::level::Level* l)
        : LevelEvent(l) {}

    const std::string& eventName() const override {
        static std::string n = "LevelUnloadEvent"; return n;
    }
};

} // namespace cursestone::event::level
