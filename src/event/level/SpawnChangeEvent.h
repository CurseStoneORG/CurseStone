// SPDX-License-Identifier: LGPL-3.0-or-later
// SpawnChangeEvent.h — ported from cn.nukkit.event.level.SpawnChangeEvent
//
// Fired when a level's spawn point changes. Holds the previous spawn
// coordinates.
#pragma once
#include "LevelEvent.h"
#include <string>
namespace atomikpe { namespace level { class Level; } }

namespace atomikpe::event::level {

class SpawnChangeEvent : public LevelEvent {
public:
    double prevX, prevY, prevZ;

    SpawnChangeEvent(atomikpe::level::Level* l, double px, double py, double pz)
        : LevelEvent(l), prevX(px), prevY(py), prevZ(pz) {}

    const std::string& eventName() const override {
        static std::string n = "SpawnChangeEvent"; return n;
    }
};

} // namespace atomikpe::event::level
