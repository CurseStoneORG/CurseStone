// SPDX-License-Identifier: LGPL-3.0-or-later
// WorldSaveEvent.h — ported from cn.nukkit.event.level.LevelSaveEvent
//
// Fired when a world is being saved to disk.
#pragma once
#include "WorldEvent.h"
#include <string>
namespace cursestone { namespace world { class World; } }

namespace cursestone::event::world {

class WorldSaveEvent : public WorldEvent {
public:
    explicit WorldSaveEvent(cursestone::world::World* l)
        : WorldEvent(l) {}

    const std::string& eventName() const override {
        static std::string n = "WorldSaveEvent"; return n;
    }
};

} // namespace cursestone::event::world
