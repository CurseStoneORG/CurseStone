// SPDX-License-Identifier: LGPL-3.0-or-later
// DoorToggleEvent.h — ported from cn.nukkit.event.block.DoorToggleEvent
//
// Fired when a door (or trapdoor / fence gate) is opened or closed by a
// player.
#pragma once
#include "BlockUpdateEvent.h"
#include <cstdint>
namespace cursestone { class Player; }
namespace cursestone::event::block {

// NOTA: no hereda Cancellable directamente — BlockUpdateEvent ya lo
// hace. Repetirlo aquí crearía un diamante no-virtual, ambiguo en
// tiempo de compilación para isCancelled()/setCancelled().
class DoorToggleEvent : public BlockUpdateEvent {
public:
    cursestone::Player* player;
    DoorToggleEvent(int32_t bx, int32_t by, int32_t bz, cursestone::Player* p)
        : BlockUpdateEvent(bx, by, bz), player(p) {}

    const std::string& eventName() const override {
        static std::string n = "DoorToggleEvent"; return n;
    }
};

} // namespace cursestone::event::block
