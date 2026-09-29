// SPDX-License-Identifier: LGPL-3.0-or-later
// LevelInitEvent.h — ported from cn.nukkit.event.level.LevelInitEvent
//
// Fired when a level is being initialized (before entities and tiles
// are loaded).
#pragma once
#include "LevelEvent.h"
#include <string>
namespace cursestone { namespace level { class Level; } }

namespace cursestone::event::level {

class LevelInitEvent : public LevelEvent {
public:
    explicit LevelInitEvent(cursestone::level::Level* l)
        : LevelEvent(l) {}

    const std::string& eventName() const override {
        static std::string n = "LevelInitEvent"; return n;
    }
};

} // namespace cursestone::event::level
