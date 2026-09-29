// SPDX-License-Identifier: LGPL-3.0-or-later
// BlockEvent.h — ported from cn.nukkit.event.block.BlockEvent
//
// Base class for all block-related events. Holds block coordinates.
#pragma once
#include "../Event.h"
#include <cstdint>
namespace cursestone::event::block {

class BlockEvent : public Event {
public:
    int32_t x, y, z;
    BlockEvent(int32_t bx, int32_t by, int32_t bz) : x(bx), y(by), z(bz) {}
    const std::string& eventName() const override {
        static std::string n = "BlockEvent"; return n;
    }
};

} // namespace cursestone::event::block
