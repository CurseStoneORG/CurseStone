// SPDX-License-Identifier: LGPL-3.0-or-later
// PlayerDropItemEvent.h — ported from cn.nukkit.event.player.PlayerDropItemEvent
#pragma once
#include "PlayerEvent.h"
#include "../Cancellable.h"
#include "../../item/ItemStack.h"
namespace atomikpe { class Player; }

namespace atomikpe::event::player {
class PlayerDropItemEvent : public PlayerEvent, public Cancellable {
public:
    atomikpe::item::ItemStack item;
    PlayerDropItemEvent(atomikpe::Player* p, const atomikpe::item::ItemStack& it)
        : PlayerEvent(p), item(it) {}
    const std::string& eventName() const override {
        static std::string n = "PlayerDropItemEvent"; return n;
    }
};
} // namespace atomikpe::event::player
