// SPDX-License-Identifier: LGPL-3.0-or-later
// ItemFrameDropItemEvent.h — ported from cn.nukkit.event.block.ItemFrameDropItemEvent
//
// Fired when an item frame drops its displayed item (typically because
// a player punched it or placed a new item in the frame).
#pragma once
#include "BlockEvent.h"
#include "../Cancellable.h"
#include "../../item/ItemStack.h"
#include <cstdint>
namespace atomikpe { class Player; }
namespace atomikpe::event::block {

class ItemFrameDropItemEvent : public BlockEvent, public Cancellable {
public:
    atomikpe::Player* player;
    atomikpe::item::ItemStack item;
    ItemFrameDropItemEvent(int32_t bx, int32_t by, int32_t bz,
                           atomikpe::Player* p,
                           const atomikpe::item::ItemStack& it)
        : BlockEvent(bx, by, bz), player(p), item(it) {}

    const std::string& eventName() const override {
        static std::string n = "ItemFrameDropItemEvent"; return n;
    }
};

} // namespace atomikpe::event::block
