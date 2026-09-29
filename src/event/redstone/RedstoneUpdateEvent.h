// SPDX-License-Identifier: LGPL-3.0-or-later
// RedstoneUpdateEvent.h — ported from cn.nukkit.event.redstone.RedstoneUpdateEvent
//
// Fired when a redstone component receives an update tick. Mirrors
// Nukkit's RedstoneUpdateEvent which simply wraps BlockUpdateEvent.
#pragma once
#include "../block/BlockUpdateEvent.h"
#include <cstdint>
#include <string>

namespace cursestone::event::redstone {

class RedstoneUpdateEvent : public cursestone::event::block::BlockUpdateEvent {
public:
    RedstoneUpdateEvent(int32_t bx, int32_t by, int32_t bz)
        : BlockUpdateEvent(bx, by, bz) {}

    const std::string& eventName() const override {
        static std::string n = "RedstoneUpdateEvent"; return n;
    }
};

} // namespace cursestone::event::redstone
