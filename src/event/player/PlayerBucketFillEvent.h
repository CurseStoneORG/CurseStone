// SPDX-License-Identifier: LGPL-3.0-or-later
// PlayerBucketFillEvent.h — ported from cn.nukkit.event.player.PlayerBucketFillEvent
#pragma once
#include "PlayerBucketEvent.h"
namespace cursestone { class Player; }

namespace cursestone::event::player {
class PlayerBucketFillEvent : public PlayerBucketEvent {
public:
    PlayerBucketFillEvent(cursestone::Player* p, const cursestone::item::ItemStack& b,
                          int bx, int by, int bz, int f)
        : PlayerBucketEvent(p, b, bx, by, bz, f) {}
    const std::string& eventName() const override {
        static std::string n = "PlayerBucketFillEvent"; return n;
    }
};
} // namespace cursestone::event::player
