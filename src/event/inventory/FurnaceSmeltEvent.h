// SPDX-License-Identifier: LGPL-3.0-or-later
// FurnaceSmeltEvent.h — ported from cn.nukkit.event.inventory.FurnaceSmeltEvent
#pragma once
#include "../block/BlockEvent.h"
#include "../Cancellable.h"
#include "../../item/ItemStack.h"
namespace atomikpe { class BlockEntityFurnace; }

namespace atomikpe::event::inventory {

class FurnaceSmeltEvent : public atomikpe::event::block::BlockEvent, public Cancellable {
public:
    atomikpe::BlockEntityFurnace* furnace;
    atomikpe::item::ItemStack source;
    atomikpe::item::ItemStack result;
    FurnaceSmeltEvent(atomikpe::BlockEntityFurnace* f,
                       const atomikpe::item::ItemStack& src,
                       const atomikpe::item::ItemStack& res)
        : BlockEvent(0, 0, 0), furnace(f), source(src), result(res) {}
    atomikpe::BlockEntityFurnace* getFurnace() { return furnace; }
    atomikpe::item::ItemStack getSource() { return source; }
    atomikpe::item::ItemStack getResult() { return result; }
    void setResult(const atomikpe::item::ItemStack& r) { result = r; }
    const std::string& eventName() const override {
        static std::string n = "FurnaceSmeltEvent"; return n;
    }
};

} // namespace atomikpe::event::inventory
