// SPDX-License-Identifier: LGPL-3.0-or-later
// FurnaceSmeltEvent.h — ported from cn.nukkit.event.inventory.FurnaceSmeltEvent
#pragma once
#include "../block/BlockEvent.h"
#include "../Cancellable.h"
#include "../../item/ItemStack.h"
namespace cursestone { class BlockEntityFurnace; }

namespace cursestone::event::inventory {

class FurnaceSmeltEvent : public cursestone::event::block::BlockEvent, public Cancellable {
public:
    cursestone::BlockEntityFurnace* furnace;
    cursestone::item::ItemStack source;
    cursestone::item::ItemStack result;
    FurnaceSmeltEvent(cursestone::BlockEntityFurnace* f,
                       const cursestone::item::ItemStack& src,
                       const cursestone::item::ItemStack& res)
        : BlockEvent(0, 0, 0), furnace(f), source(src), result(res) {}
    cursestone::BlockEntityFurnace* getFurnace() { return furnace; }
    cursestone::item::ItemStack getSource() { return source; }
    cursestone::item::ItemStack getResult() { return result; }
    void setResult(const cursestone::item::ItemStack& r) { result = r; }
    const std::string& eventName() const override {
        static std::string n = "FurnaceSmeltEvent"; return n;
    }
};

} // namespace cursestone::event::inventory
