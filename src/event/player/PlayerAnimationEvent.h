/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

 * CurseStone | Open Source Server Software
 *
 * Copyright (C) 2026 CurseStoneORG
 *
 * Developed by CurseStoneORG.
 * Built for an open and extensible Minecraft server ecosystem.
 *
 * Licensed under the GNU Lesser General Public License v3.0 or later.
 * You may study, modify, and redistribute this software under its license.
 *
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 * Repository: https://github.com/CurseStoneORG/CurseStone
 *
 */
// SPDX-License-Identifier: LGPL-3.0-or-later
// PlayerAnimationEvent.h — fired when a client animation packet arrives
// (protocol 84 ANIMATE): arm swings on punching, mining and placing,
// and the wake-up action when leaving a bed view.
//
// This is a purely visual notification. Cancelling stops the server
// from relaying the animation to the other viewers of the player; it
// does NOT touch combat logic — the swing arm packet that rides along
// with a punch has already been resolved by the interaction path, and
// damage handling never consults this event.
//
// Animation identifiers mirror the protocol-84 action table; unknown
// ids still fire (the packet path carries them verbatim) so plugins can
// observe whatever the client sent.
#pragma once
#include "PlayerEvent.h"
#include "../Cancellable.h"
#include <cstdint>
namespace cursestone {
class Player;
}

namespace cursestone::event::player {

class PlayerAnimationEvent : public PlayerEvent, public Cancellable {
  public:
    // Protocol-84 animate actions observed on this server version.
    static constexpr uint32_t ANIMATION_SWING_ARM = 1;
    static constexpr uint32_t ANIMATION_WAKE_UP = 3;

    PlayerAnimationEvent(cursestone::Player* p, uint32_t animationId)
        : PlayerEvent(p), animation_id_(animationId) {
    }

    // Raw animation id from the packet (see the ANIMATION_* constants).
    uint32_t animationId() const {
        return animation_id_;
    }

    const std::string& eventName() const override {
        static std::string n = "PlayerAnimationEvent";
        return n;
    }

    // Human-readable animation name shared by the plugin bindings.
    static const char* animationName(uint32_t id) {
        if (id == ANIMATION_SWING_ARM)
            return "swing_arm";
        if (id == ANIMATION_WAKE_UP)
            return "wake_up";
        return "unknown";
    }

  private:
    uint32_t animation_id_;
};

} // namespace cursestone::event::player
