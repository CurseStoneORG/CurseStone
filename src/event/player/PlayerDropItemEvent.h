// SPDX-License-Identifier: LGPL-3.0-or-later
// PlayerDropItemEvent.h — ported from cn.nukkit.event.player.PlayerDropItemEvent
#pragma once
#include "PlayerEvent.h"
#include "../Cancellable.h"
#include "../../item/ItemStack.h"
namespace cursestone { class Player; }

namespace cursestone::event::player {
class PlayerDropItemEvent : public PlayerEvent, public Cancellable {
public:
    cursestone::item::ItemStack item;
    PlayerDropItemEvent(cursestone::Player* p, const cursestone::item::ItemStack& it)
        : PlayerEvent(p), item(it) {}
    const std::string& eventName() const override {
        static std::string n = "PlayerDropItemEvent"; return n;
    }
};
} // namespace cursestone::event::player
