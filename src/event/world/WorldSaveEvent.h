// SPDX-License-Identifier: LGPL-3.0-or-later
// LevelSaveEvent.h — ported from cn.nukkit.event.level.LevelSaveEvent
//
// Fired when a level is being saved to disk.
#pragma once
#include "WorldEvent.h"
#include <string>
namespace cursestone { namespace world { class World; } }

namespace cursestone::event::world {

class LevelSaveEvent : public LevelEvent {
public:
    explicit LevelSaveEvent(cursestone::world::World* l)
        : LevelEvent(l) {}

    const std::string& eventName() const override {
        static std::string n = "LevelSaveEvent"; return n;
    }
};

} // namespace cursestone::event::world
