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
// PlayerToggleSneakEvent.h — fired when a player is about to START
// sneaking (true) or STOP sneaking (false).
//
// Timing: the event runs before the pose flag changes, so cancelling it
// keeps the previous sneak state and the client is resynced with it.
// The server already refuses to fire for a no-op toggle (state equal to
// the requested one), so listeners never see duplicate transitions.
//
// Sneak and sprint are independent mechanics: this event never overlaps
// PlayerToggleSprintEvent.
#pragma once
#include "PlayerEvent.h"
#include "../Cancellable.h"
namespace cursestone {
class Player;
}

namespace cursestone::event::player {

class PlayerToggleSneakEvent : public PlayerEvent, public Cancellable {
  public:
    PlayerToggleSneakEvent(cursestone::Player* p, bool sneaking)
        : PlayerEvent(p), sneaking_(sneaking) {
    }

    // The NEW state the toggle is asking for: true = starts sneaking,
    // false = stops sneaking.
    bool isSneaking() const {
        return sneaking_;
    }

    const std::string& eventName() const override {
        static std::string n = "PlayerToggleSneakEvent";
        return n;
    }

  private:
    bool sneaking_;
};

} // namespace cursestone::event::player
