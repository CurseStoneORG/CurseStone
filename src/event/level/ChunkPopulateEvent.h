// SPDX-License-Identifier: LGPL-3.0-or-later
// ChunkPopulateEvent.h — ported from cn.nukkit.event.level.ChunkPopulateEvent
//
// Fired after a newly generated chunk is populated with features
// (ores, trees, grass, etc.).
#pragma once
#include "ChunkEvent.h"
#include <string>
namespace atomikpe { namespace level { class Level; } }

namespace atomikpe::event::level {

class ChunkPopulateEvent : public ChunkEvent {
public:
    ChunkPopulateEvent(atomikpe::level::Level* l, void* c)
        : ChunkEvent(l, c) {}

    const std::string& eventName() const override {
        static std::string n = "ChunkPopulateEvent"; return n;
    }
};

} // namespace atomikpe::event::level
