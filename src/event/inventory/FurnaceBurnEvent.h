// SPDX-License-Identifier: LGPL-3.0-or-later
// FurnaceBurnEvent.h — ported from cn.nukkit.event.inventory.FurnaceBurnEvent
#pragma once
#include "../block/BlockEvent.h"
#include "../Cancellable.h"
#include "../../item/ItemStack.h"
namespace cursestone { class BlockEntityFurnace; }

namespace cursestone::event::inventory {

class FurnaceBurnEvent : public cursestone::event::block::BlockEvent, public Cancellable {
public:
    cursestone::BlockEntityFurnace* furnace;
    cursestone::item::ItemStack fuel;
    int16_t burnTime;
    bool burning;
    FurnaceBurnEvent(cursestone::BlockEntityFurnace* f, const cursestone::item::ItemStack& fuel,
                      int16_t bt)
        : BlockEvent(0, 0, 0), furnace(f), fuel(fuel), burnTime(bt), burning(true) {}
    cursestone::BlockEntityFurnace* getFurnace() { return furnace; }
    cursestone::item::ItemStack getFuel() { return fuel; }
    int16_t getBurnTime() { return burnTime; }
    void setBurnTime(int16_t bt) { burnTime = bt; }
    bool isBurning() { return burning; }
    void setBurning(bool b) { burning = b; }
    const std::string& eventName() const override {
        static std::string n = "FurnaceBurnEvent"; return n;
    }
};

} // namespace cursestone::event::inventory
