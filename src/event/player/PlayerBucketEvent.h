// SPDX-License-Identifier: LGPL-3.0-or-later
// PlayerBucketEvent.h — ported from cn.nukkit.event.player.PlayerBucketEvent
//
// Base class for player bucket events (fill / empty). Holds the
// bucket ItemStack and the targeted block position + face.
#pragma once
#include "PlayerEvent.h"
#include "../Cancellable.h"
#include "../../item/ItemStack.h"
namespace atomikpe { class Player; }

namespace atomikpe::event::player {
class PlayerBucketEvent : public PlayerEvent, public Cancellable {
public:
    atomikpe::item::ItemStack bucket;
    int blockX = 0;
    int blockY = 0;
    int blockZ = 0;
    int face = 0;
    PlayerBucketEvent(atomikpe::Player* p, const atomikpe::item::ItemStack& b,
                      int bx, int by, int bz, int f)
        : PlayerEvent(p), bucket(b), blockX(bx), blockY(by), blockZ(bz), face(f) {}
    const std::string& eventName() const override {
        static std::string n = "PlayerBucketEvent"; return n;
    }
};
} // namespace atomikpe::event::player
