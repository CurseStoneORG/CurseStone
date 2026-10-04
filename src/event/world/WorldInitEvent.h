// SPDX-License-Identifier: LGPL-3.0-or-later
// LevelInitEvent.h — ported from cn.nukkit.event.level.LevelInitEvent
//
// Fired when a level is being initialized (before entities and tiles
// are loaded).
#pragma once
#include "WorldEvent.h"
#include <string>
namespace cursestone { namespace world { class World; } }

namespace cursestone::event::world {

class LevelInitEvent : public LevelEvent {
public:
    explicit LevelInitEvent(cursestone::world::World* l)
        : LevelEvent(l) {}

    const std::string& eventName() const override {
        static std::string n = "LevelInitEvent"; return n;
    }
};

} // namespace cursestone::event::world
