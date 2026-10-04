// SPDX-License-Identifier: LGPL-3.0-or-later
// WorldInitEvent.h — ported from cn.nukkit.event.level.LevelInitEvent
//
// Fired when a world is being initialized (before entities and tiles
// are loaded).
#pragma once
#include "WorldEvent.h"
#include <string>
namespace cursestone { namespace world { class World; } }

namespace cursestone::event::world {

class WorldInitEvent : public WorldEvent {
public:
    explicit WorldInitEvent(cursestone::world::World* l)
        : WorldEvent(l) {}

    const std::string& eventName() const override {
        static std::string n = "WorldInitEvent"; return n;
    }
};

} // namespace cursestone::event::world
