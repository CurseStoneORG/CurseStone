// SPDX-License-Identifier: LGPL-3.0-or-later
// ChunkUnloadEvent.h — ported from cn.nukkit.event.level.ChunkUnloadEvent
//
// Fired before a chunk is unloaded from memory. Cancellable.
#pragma once
#include "ChunkEvent.h"
#include "../Cancellable.h"
#include <string>
namespace cursestone { namespace level { class Level; } }

namespace cursestone::event::level {

class ChunkUnloadEvent : public ChunkEvent, public Cancellable {
public:
    ChunkUnloadEvent(cursestone::level::Level* l, void* c)
        : ChunkEvent(l, c) {}

    const std::string& eventName() const override {
        static std::string n = "ChunkUnloadEvent"; return n;
    }
};

} // namespace cursestone::event::level
