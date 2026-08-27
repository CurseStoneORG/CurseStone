// SPDX-License-Identifier: LGPL-3.0-or-later
// BlockFormEvent.h — ported from cn.nukkit.event.block.BlockFormEvent
//
// Fired when a block is formed in the world (e.g. snow forming, ice
// freezing, concrete powder solidifying). Uses the parent's newState
// coordinates to describe the new state.
#pragma once
#include "BlockGrowEvent.h"
#include "../Cancellable.h"
#include <cstdint>
namespace atomikpe::event::block {

class BlockFormEvent : public BlockGrowEvent, public Cancellable {
public:
    BlockFormEvent(int32_t bx, int32_t by, int32_t bz,
                   int32_t nsx, int32_t nsy, int32_t nsz)
        : BlockGrowEvent(bx, by, bz, nsx, nsy, nsz) {}

    const std::string& eventName() const override {
        static std::string n = "BlockFormEvent"; return n;
    }
};

} // namespace atomikpe::event::block
