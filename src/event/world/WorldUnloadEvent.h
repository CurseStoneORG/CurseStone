// SPDX-License-Identifier: LGPL-3.0-or-later
// WorldUnloadEvent.h — ported from cn.nukkit.event.level.LevelUnloadEvent
//
// Fired before a world is unloaded from the server. Cancellable.
#pragma once
#include "WorldEvent.h"
#include "../Cancellable.h"
#include <string>
namespace cursestone { namespace world { class World; } }

namespace cursestone::event::world {

class WorldUnloadEvent : public WorldEvent, public Cancellable {
public:
    explicit WorldUnloadEvent(cursestone::world::World* l)
        : WorldEvent(l) {}

    const std::string& eventName() const override {
        static std::string n = "WorldUnloadEvent"; return n;
    }
};

} // namespace cursestone::event::world
