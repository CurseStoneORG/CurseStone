// SPDX-License-Identifier: LGPL-3.0-or-later
// ChunkEvent.h — ported from cn.nukkit.event.level.ChunkEvent
//
// Base class for all chunk-related events. Holds a reference to the
// owning level and an opaque chunk pointer.
#pragma once
#include "LevelEvent.h"
#include <string>
namespace atomikpe { namespace level { class Level; } }

namespace atomikpe::event::level {

class ChunkEvent : public LevelEvent {
public:
    void* chunk;

    ChunkEvent(atomikpe::level::Level* l, void* c)
        : LevelEvent(l), chunk(c) {}

    const std::string& eventName() const override {
        static std::string n = "ChunkEvent"; return n;
    }
};

} // namespace atomikpe::event::level
