// SPDX-License-Identifier: LGPL-3.0-or-later
// BlockSpreadEvent.h — ported from cn.nukkit.event.block.BlockSpreadEvent
//
// Fired when a block spreads to another block (e.g. grass spreading to
// dirt, mushrooms spreading). Holds both the source coordinates and the
// new state coordinates.
#pragma once
#include "BlockFormEvent.h"
#include "../Cancellable.h"
#include <cstdint>
namespace atomikpe::event::block {

class BlockSpreadEvent : public BlockFormEvent, public Cancellable {
public:
    int32_t sourceX, sourceY, sourceZ;
    BlockSpreadEvent(int32_t bx, int32_t by, int32_t bz,
                     int32_t sx, int32_t sy, int32_t sz,
                     int32_t nsx, int32_t nsy, int32_t nsz)
        : BlockFormEvent(bx, by, bz, nsx, nsy, nsz),
          sourceX(sx), sourceY(sy), sourceZ(sz) {}

    const std::string& eventName() const override {
        static std::string n = "BlockSpreadEvent"; return n;
    }
};

} // namespace atomikpe::event::block
