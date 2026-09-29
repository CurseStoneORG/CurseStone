// SPDX-License-Identifier: LGPL-3.0-or-later
// BlockUpdateEvent.h — ported from cn.nukkit.event.block.BlockUpdateEvent
//
// Fired when a block receives a scheduled update tick.
#pragma once
#include "BlockEvent.h"
#include "../Cancellable.h"
#include <cstdint>
namespace cursestone::event::block {

class BlockUpdateEvent : public BlockEvent, public Cancellable {
public:
    BlockUpdateEvent(int32_t bx, int32_t by, int32_t bz)
        : BlockEvent(bx, by, bz) {}

    const std::string& eventName() const override {
        static std::string n = "BlockUpdateEvent"; return n;
    }
};

} // namespace cursestone::event::block
