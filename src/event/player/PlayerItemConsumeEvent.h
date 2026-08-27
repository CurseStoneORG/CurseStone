// SPDX-License-Identifier: LGPL-3.0-or-later
// PlayerItemConsumeEvent.h — ported from cn.nukkit.event.player.PlayerItemConsumeEvent
#pragma once
#include "PlayerEvent.h"
#include "../Cancellable.h"
#include "../../item/ItemStack.h"
namespace atomikpe { class Player; }

namespace atomikpe::event::player {
class PlayerItemConsumeEvent : public PlayerEvent, public Cancellable {
public:
    atomikpe::item::ItemStack item;
    PlayerItemConsumeEvent(atomikpe::Player* p, const atomikpe::item::ItemStack& it)
        : PlayerEvent(p), item(it) {}
    const std::string& eventName() const override {
        static std::string n = "PlayerItemConsumeEvent"; return n;
    }
};
} // namespace atomikpe::event::player
