// SPDX-License-Identifier: LGPL-3.0-or-later
// LeavesDecayEvent.h — ported from cn.nukkit.event.block.LeavesDecayEvent
//
// Fired when a leaf block is about to decay (typically because no log
// is nearby).
#pragma once
#include "BlockEvent.h"
#include "../Cancellable.h"
#include <cstdint>
namespace atomikpe::event::block {

class LeavesDecayEvent : public BlockEvent, public Cancellable {
public:
    LeavesDecayEvent(int32_t bx, int32_t by, int32_t bz)
        : BlockEvent(bx, by, bz) {}

    const std::string& eventName() const override {
        static std::string n = "LeavesDecayEvent"; return n;
    }
};

} // namespace atomikpe::event::block
