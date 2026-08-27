// SPDX-License-Identifier: LGPL-3.0-or-later
// PlayerInteractEvent.h — ported from cn.nukkit.event.player.PlayerInteractEvent
#pragma once
#include "PlayerEvent.h"
#include "../Cancellable.h"
#include "../../item/ItemStack.h"
namespace atomikpe { class Player; }

namespace atomikpe::event::player {
class PlayerInteractEvent : public PlayerEvent, public Cancellable {
public:
    static constexpr int LEFT_CLICK_BLOCK  = 0;
    static constexpr int RIGHT_CLICK_BLOCK = 1;
    static constexpr int LEFT_CLICK_AIR    = 2;
    static constexpr int RIGHT_CLICK_AIR   = 3;
    static constexpr int PHYSICAL          = 4;

    int action = 0;
    int blockX = 0;
    int blockY = 0;
    int blockZ = 0;
    int face = 0;
    atomikpe::item::ItemStack item;

    PlayerInteractEvent(atomikpe::Player* p, int act, int bx, int by, int bz,
                        int f, const atomikpe::item::ItemStack& it)
        : PlayerEvent(p), action(act), blockX(bx), blockY(by), blockZ(bz),
          face(f), item(it) {}
    const std::string& eventName() const override {
        static std::string n = "PlayerInteractEvent"; return n;
    }
};
} // namespace atomikpe::event::player
