// SPDX-License-Identifier: LGPL-3.0-or-later
// PlayerItemHeldEvent.h — ported from cn.nukkit.event.player.PlayerItemHeldEvent
#pragma once
#include "PlayerEvent.h"
#include "../Cancellable.h"
#include "../../item/ItemStack.h"
namespace atomikpe { class Player; }

namespace atomikpe::event::player {
class PlayerItemHeldEvent : public PlayerEvent, public Cancellable {
public:
    atomikpe::item::ItemStack item;
    int slot = 0;
    int hotbarSlot = 0;
    PlayerItemHeldEvent(atomikpe::Player* p, const atomikpe::item::ItemStack& it,
                        int s, int hs)
        : PlayerEvent(p), item(it), slot(s), hotbarSlot(hs) {}
    const std::string& eventName() const override {
        static std::string n = "PlayerItemHeldEvent"; return n;
    }
};
} // namespace atomikpe::event::player
