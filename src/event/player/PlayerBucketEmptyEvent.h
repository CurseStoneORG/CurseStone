// SPDX-License-Identifier: LGPL-3.0-or-later
// PlayerBucketEmptyEvent.h — ported from cn.nukkit.event.player.PlayerBucketEmptyEvent
#pragma once
#include "PlayerBucketEvent.h"
namespace atomikpe { class Player; }

namespace atomikpe::event::player {
class PlayerBucketEmptyEvent : public PlayerBucketEvent {
public:
    PlayerBucketEmptyEvent(atomikpe::Player* p, const atomikpe::item::ItemStack& b,
                           int bx, int by, int bz, int f)
        : PlayerBucketEvent(p, b, bx, by, bz, f) {}
    const std::string& eventName() const override {
        static std::string n = "PlayerBucketEmptyEvent"; return n;
    }
};
} // namespace atomikpe::event::player
