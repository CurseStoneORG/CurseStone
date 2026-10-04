// SPDX-License-Identifier: LGPL-3.0-or-later
// SpawnChangeEvent.h — ported from cn.nukkit.event.level.SpawnChangeEvent
//
// Fired when a level's spawn point changes. Holds the previous spawn
// coordinates.
#pragma once
#include "WorldEvent.h"
#include <string>
namespace cursestone { namespace world { class World; } }

namespace cursestone::event::world {

class SpawnChangeEvent : public LevelEvent {
public:
    double prevX, prevY, prevZ;

    SpawnChangeEvent(cursestone::world::World* l, double px, double py, double pz)
        : LevelEvent(l), prevX(px), prevY(py), prevZ(pz) {}

    const std::string& eventName() const override {
        static std::string n = "SpawnChangeEvent"; return n;
    }
};

} // namespace cursestone::event::world
