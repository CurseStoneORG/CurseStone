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
// PlayerToggleSprintEvent.h — fired when a player is about to START
// sprinting (true) or STOP sprinting (false), driven by the client's
// action packets.
//
// Timing: the event runs before the sprint flag changes, so cancelling
// it keeps the previous sprint state and the pose flags are re-sent so
// the client's prediction does not desync. The server only fires the
// event on a real transition (a start while already sprinting or a stop
// while already walking is silently ignored). The vanilla food gate —
// starting a sprint needs more than six food points — is applied before
// the event, exactly like every other server-side refusal.
#pragma once
#include "PlayerEvent.h"
#include "../Cancellable.h"
namespace cursestone {
class Player;
}

namespace cursestone::event::player {

class PlayerToggleSprintEvent : public PlayerEvent, public Cancellable {
  public:
    PlayerToggleSprintEvent(cursestone::Player* p, bool sprinting)
        : PlayerEvent(p), sprinting_(sprinting) {
    }

    // The NEW state the toggle is asking for: true = starts sprinting,
    // false = stops sprinting.
    bool isSprinting() const {
        return sprinting_;
    }

    const std::string& eventName() const override {
        static std::string n = "PlayerToggleSprintEvent";
        return n;
    }

  private:
    bool sprinting_;
};

} // namespace cursestone::event::player
