// SPDX-License-Identifier: LGPL-3.0-or-later
// LevelSaveEvent.h — ported from cn.nukkit.event.level.LevelSaveEvent
//
// Fired when a level is being saved to disk.
#pragma once
#include "LevelEvent.h"
#include <string>
namespace atomikpe { namespace level { class Level; } }

namespace atomikpe::event::level {

class LevelSaveEvent : public LevelEvent {
public:
    explicit LevelSaveEvent(atomikpe::level::Level* l)
        : LevelEvent(l) {}

    const std::string& eventName() const override {
        static std::string n = "LevelSaveEvent"; return n;
    }
};

} // namespace atomikpe::event::level
