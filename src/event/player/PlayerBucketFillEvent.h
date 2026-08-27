// SPDX-License-Identifier: LGPL-3.0-or-later
// PlayerBucketFillEvent.h — ported from cn.nukkit.event.player.PlayerBucketFillEvent
#pragma once
#include "PlayerBucketEvent.h"
namespace atomikpe { class Player; }

namespace atomikpe::event::player {
class PlayerBucketFillEvent : public PlayerBucketEvent {
public:
    PlayerBucketFillEvent(atomikpe::Player* p, const atomikpe::item::ItemStack& b,
                          int bx, int by, int bz, int f)
        : PlayerBucketEvent(p, b, bx, by, bz, f) {}
    const std::string& eventName() const override {
        static std::string n = "PlayerBucketFillEvent"; return n;
    }
};
} // namespace atomikpe::event::player
