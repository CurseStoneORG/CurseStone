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
// PlayerGamemodeChangeEvent.h — fired when a player is ABOUT to switch
// game modes, before any state changes.
//
// CurseStone supports the survival (0), creative (1) and adventure (2)
// modes of the 0.15 protocol; the spectator nibble (3) passes through
// the wire mask but no gameplay surface publishes it. Both the current
// and the requested mode are readable; cancelling keeps the previous
// mode active with no side effects (no catalog refresh, no inventory
// resync, no fall-damage reset).
//
// The event never fires when the requested mode equals the current one
// — no listener traffic for no-op switches, and the caller's post-change
// sync packets still keep working unchanged.
#pragma once
#include "PlayerEvent.h"
#include "../Cancellable.h"
#include <cstdint>
namespace cursestone {
class Player;
}

namespace cursestone::event::player {

class PlayerGamemodeChangeEvent : public PlayerEvent, public Cancellable {
  public:
    PlayerGamemodeChangeEvent(cursestone::Player* p, int32_t oldGamemode, int32_t newGamemode)
        : PlayerEvent(p), old_gamemode_(oldGamemode), new_gamemode_(newGamemode) {
    }

    // Mode the player is leaving (0 survival, 1 creative, 2 adventure).
    int32_t getOldGamemode() const {
        return old_gamemode_;
    }
    // Mode the player requested (same value table).
    int32_t getNewGamemode() const {
        return new_gamemode_;
    }

    const std::string& eventName() const override {
        static std::string n = "PlayerGamemodeChangeEvent";
        return n;
    }

    // Human-readable mode name shared by the plugin bindings and docs.
    static const char* modeName(int32_t gamemode) {
        switch (gamemode) {
        case 0:
            return "survival";
        case 1:
            return "creative";
        case 2:
            return "adventure";
        case 3:
            return "spectator";
        default:
            return "unknown";
        }
    }

  private:
    int32_t old_gamemode_;
    int32_t new_gamemode_;
};

} // namespace cursestone::event::player
