// SPDX-License-Identifier: LGPL-3.0-or-later
// ChunkUnloadEvent.h — ported from cn.nukkit.event.level.ChunkUnloadEvent
//
// Fired before a chunk is unloaded from memory. Cancellable.
#pragma once
#include "ChunkEvent.h"
#include "../Cancellable.h"
#include <string>
namespace cursestone { namespace world { class World; } }

namespace cursestone::event::world {

class ChunkUnloadEvent : public ChunkEvent, public Cancellable {
public:
    ChunkUnloadEvent(cursestone::world::World* l, void* c)
        : ChunkEvent(l, c) {}

    const std::string& eventName() const override {
        static std::string n = "ChunkUnloadEvent"; return n;
    }
};

} // namespace cursestone::event::world
