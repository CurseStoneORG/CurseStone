// SPDX-License-Identifier: LGPL-3.0-or-later
// BlockGrowEvent.h — ported from cn.nukkit.event.block.BlockGrowEvent
//
// Fired when a block grows (e.g. crops). Holds the coordinates of the
// growing block plus the coordinates of the new state that will replace it.
#pragma once
#include "BlockEvent.h"
#include "../Cancellable.h"
#include <cstdint>
namespace atomikpe::event::block {

class BlockGrowEvent : public BlockEvent, public Cancellable {
public:
    int32_t newStateX, newStateY, newStateZ;
    BlockGrowEvent(int32_t bx, int32_t by, int32_t bz,
                   int32_t nsx, int32_t nsy, int32_t nsz)
        : BlockEvent(bx, by, bz),
          newStateX(nsx), newStateY(nsy), newStateZ(nsz) {}

    const std::string& eventName() const override {
        static std::string n = "BlockGrowEvent"; return n;
    }
};

} // namespace atomikpe::event::block
